# frozen_string_literal: true

require "json"
require "tmpdir"
require "securerandom"

module Stitch
  ST_NOT_RUN = 0
  ST_OK = 1
  ST_BAIL = 2
  ST_SKIP_MISSING = 3
  ST_SKIP_INVALID = 4
  MAX_NODES = 256
  STRINGS_SIZE = 65_536
  RUNTIME_DIR = File.expand_path("..", __dir__)
  # set by the engine's `fuzz` command for the workers it starts (not for exec / triage)
  FUZZING = ENV["STITCH_FUZZING"] == "1"

  # Raised by Stitch.bail: this call stops, the program continues. Not a bug. (An Exception, not a
  # StandardError: a library's `rescue => e` does not swallow it.)
  class Bail < Exception # rubocop:disable Lint/InheritException
    attr_reader :reason

    def initialize(reason = "bail")
      super(reason)
      @reason = reason
    end
  end

  class CheckFailed < Exception # rubocop:disable Lint/InheritException
    attr_reader :what

    def initialize(what)
      super(what)
      @what = what
    end
  end

  # A bug was found in the current program (report already printed).
  class Finding < Exception; end # rubocop:disable Lint/InheritException

  def self.fnv1a(s)
    h = 2_166_136_261
    s.each_byte { |c| h = ((h ^ c) * 16_777_619) & 0xFFFFFFFF }
    h.zero? ? 1 : h
  end

  # ============================================================================ registry
  class Endpoint
    attr_reader :fn, :name, :bails_on, :file, :line, :args, :outputs, :multi

    def initialize(name, specs, returns, bails_on, fn)
      @fn = fn
      @name = name.to_s
      @bails_on = Array(bails_on)
      path, @line = fn.source_location
      @file = Stitch.rel_path(path || "?")
      @args = specs.map do |pname, spec|
        kind, s = Stitch.resolve(spec, "stub #{@name}, parameter '#{pname}'")
        [pname.to_s, kind, s]
      end
      @multi = returns.is_a?(Array)
      outs = returns.nil? ? [] : Array(returns)
      @outputs = outs.map do |r|
        r = NewSpec.new(r) if r.is_a?(ObjectType)
        raise TypeError, "stub #{@name}: outputs are New[T] (or New[T] | nil), got #{r.inspect}" unless r.is_a?(NewSpec)

        [r.type, r.optional]
      end
      raise TypeError, "stub #{@name}: at most 16 outputs" if @outputs.size > 16
      return if fn.lambda? || fn.arity.negative? || fn.arity == @args.size

      raise TypeError, "stub #{@name}: the block takes #{fn.arity} arguments, the spec lists #{@args.size}"
    end

    def inputs = @args.select { |_, k, _| k == :obj }.map { |n, _, s| [n, s] }
    def params = @args.select { |_, k, _| k == :param }.map { |n, _, s| [n, s] }

    def bails_on?(ex) = @bails_on.any? { |c| ex.is_a?(c) }
  end

  def self.rel_path(path)
    root = ENV.fetch("STITCH_PROJECT", Dir.pwd)
    path.start_with?("#{root}/") ? path[(root.size + 1)..] : path
  end

  class Registry
    attr_reader :objects, :datatypes, :endpoints, :by_name, :inits, :dicts

    def initialize
      @objects = []
      @datatypes = {}
      @endpoints = []
      @by_name = {}
      @inits = []
      @dicts = Hash.new { |h, k| h[k] = [] }
    end

    def object(name, drop = nil)
      o = @objects.find { |x| x.name == name }
      if o
        o.drop = drop if drop
        return o
      end
      o = ObjectType.new(name, drop)
      @objects << o
      o
    end

    def datatype(name) = (@datatypes[name] ||= Datatype.new(name))

    def add_endpoint(ep)
      raise ArgumentError, "duplicate stub name '#{ep.name}' (#{@by_name[ep.name].file} and #{ep.file})" if @by_name.key?(ep.name)

      ep.inputs.each { |_, s| adopt(s.type) }
      ep.outputs.each { |t, _| adopt(t) }
      ep.params.each { |_, p| datatype(p.datatype) if p.datatype }
      @endpoints << ep
      @by_name[ep.name] = ep
    end

    def adopt(type)
      return if @objects.any? { |o| o.equal?(type) }
      raise ArgumentError, "two different object types are named '#{type.name}'" if @objects.any? { |o| o.name == type.name }

      @objects << type
    end

    def dt_list = @datatypes.values

    def manifest
      JSON.generate({
        "version" => 2,
        "objects" => @objects.map { |o| { "name" => o.name, "drop" => !o.drop.nil? } },
        "datatypes" => dt_list.map { |d| { "name" => d.name, "decoder" => !d.decoder.nil? } },
        "endpoints" => @endpoints.map do |e|
          { "name" => e.name, "file" => e.file,
            "inputs" => e.inputs.map { |n, s| { "type" => s.type.name, "mode" => s.mode, "name" => n } },
            "params" => e.params.map { |n, p| p.manifest(n) },
            "outputs" => e.outputs.map { |t, o| { "type" => t.name, "optional" => o } } }
        end
      })
    end
  end

  REG = Registry.new

  # ============================================================================ execution
  Slot = Struct.new(:type, :value, :alive, :gen, :node, :out, :attrs)
  # attribute value that links to another object (dies with it)
  Ref = Struct.new(:slot, :gen)
  MISSING = Object.new.freeze

  def self.preview(b, n = 160)
    s = +""
    b.byteslice(0, n).each_byte do |c|
      s << if c == 34 || c == 92 then "\\#{c.chr}"
           elsif c == 10 then "\\n"
           elsif c >= 32 && c < 127 then c.chr
           else format("\\x%02x", c)
           end
    end
    "\"#{s}\"#{b.bytesize > n ? '...' : ''}"
  end

  class Runtime
    attr_accessor :trace, :plan, :globals, :in_exec, :cur_node
    attr_reader :reg

    def initialize(reg = REG)
      @reg = reg
      @trace = false
      @plan = nil
      @in_exec = false
      @cur_node = -1
      @pool = []
      @obj_of = {}
      @globals = {}
      @pending = []
      @cur_in_slots = []
      @cur_consumed = 0
      @last_miss = 0
      @gen_counter = 1
      @tmp_files = []
      @exc_bail = ENV["STITCH_EXCEPTIONS"] == "bail"
      @plan_src_next = 0
      reset_results(0)
    end

    # ------------------------------------------------------------------ results
    def reset_results(n)
      @n_nodes = n
      @current = -1
      @crash_pc = 0
      @status = Array.new(n, 0)
      @n_out = Array.new(n, 0)
      @out_mask = Array.new(n, 0)
      @reason = Array.new(n, 0)
      @miss = Array.new(n, 0)
      @consumed = Array.new(n, 0)
      @strings = +"".b
      @strings_seen = {}
    end

    def intern(s)
      b = s.to_s.b
      h = Stitch.fnv1a(b)
      return h if @strings_seen.key?(h)

      b = b.byteslice(0, 512)
      if @strings.bytesize + 6 + b.bytesize <= STRINGS_SIZE
        @strings << [h, b.bytesize].pack("L<S<") << b
        @strings_seen[h] = true
      end
      h
    end

    def results_bytes
      o = [@n_nodes, @current, @crash_pc & 0xFFFFFFFFFFFFFFFF].pack("L<l<Q<")
      @n_nodes.times do |i|
        o << [@status[i], @n_out[i], @out_mask[i], @reason[i], @miss[i], @consumed[i]].pack("CCS<L<L<L<")
      end
      o << [@strings.bytesize].pack("L<") << @strings
      o << Instrument.edges_bytes << Instrument.cmps_bytes
      o
    end

    # ------------------------------------------------------------------ objects + attributes
    def live(i)
      return nil if i.negative? || i >= @pool.size

      s = @pool[i]
      s.alive ? s : nil
    end

    def slot_of(obj)
      (@pool.size - 1).downto(0) do |i|
        s = @pool[i]
        return i if s.alive && s.value.equal?(obj)
      end
      -1
    end

    def note_miss(key)
      @last_miss = intern(key.to_s) if @in_exec
    end

    def wrap(v)
      return v if v.nil? || v.is_a?(Numeric) || v.is_a?(String) || v.is_a?(Symbol) || v == true || v == false

      i = slot_of(v)
      i >= 0 ? Ref.new(i, @pool[i].gen) : v
    end

    def unwrap(v)
      return v unless v.is_a?(Ref)

      s = live(v.slot)
      s && s.gen == v.gen ? s.value : nil
    end

    def attr_set(obj, key, value)
      i = slot_of(obj)
      if i >= 0
        @pool[i].attrs[key] = wrap(value)
      else
        @pending << [obj, key, wrap(value)]
      end
    end

    def attr_get(obj, key, default = MISSING)
      i = slot_of(obj)
      if i >= 0 && @pool[i].attrs.key?(key)
        raw = @pool[i].attrs[key]
        v = unwrap(raw)
        return v unless v.nil? && raw.is_a?(Ref)
      end
      if i.negative?
        @pending.reverse_each { |o, k, v| return unwrap(v) if o.equal?(obj) && k == key }
      end
      if default.equal?(MISSING)
        note_miss(key)
        return nil
      end
      default
    end

    def attr_has(obj, key)
      i = slot_of(obj)
      i >= 0 && @pool[i].attrs.key?(key)
    end

    def attr_del(obj, key)
      i = slot_of(obj)
      @pool[i].attrs.delete(key) if i >= 0
    end

    def consume(obj)
      i = slot_of(obj)
      @cur_in_slots.each_with_index { |s, k| @cur_consumed |= 1 << k if s == i }
    end

    # ------------------------------------------------------------------ params
    def temp_file(data)
      dir = ENV["STITCH_TMPDIR"] || Dir.tmpdir
      path = File.join(dir, "stitch-#{Process.pid}-#{SecureRandom.hex(6)}")
      File.binwrite(path, data)
      @tmp_files << path
      path
    end

    def new_source(buf, cap)
      s = Source.new(buf, cap: cap)
      if @plan && @in_exec && @cur_node >= 0
        sid = @plan_src_next
        @plan_src_next += 1
        node = @cur_node
        s.log = ->(op, v, is_bytes) { plan_draw(node, sid, op, v, is_bytes) }
      end
      s
    end

    def decode_param(dtname, raw, form, mx)
      dt = dtname ? @reg.datatypes[dtname] : nil
      return run_decoder(dt, Source.new(raw, cap: [raw.bytesize, mx].max), mx) if form == 1 && dt&.decoder

      raw.byteslice(0, mx).b
    end

    def run_decoder(dt, src, mx = nil)
      v = begin
        dt.decoder.call(src)
      rescue Exception # rubocop:disable Lint/RescueException
        "".b
      end
      v = v.to_s unless v.is_a?(String)
      v = v.b
      mx ? v.byteslice(0, mx) : v
    end

    def describe(p, raw, form)
      case p
      when BufParam
        v = p.raw_value(raw, form, self)
        "#{p.ctype}[#{v.bytesize}#{form == 1 ? ',decoded' : ''}]#{Stitch.preview(v)}"
      when SrcParam
        "Src[#{raw.bytesize}]#{raw.byteslice(0, 48).unpack1('H*')}#{raw.bytesize > 48 ? '...' : ''}"
      else
        p.decode(raw, form, self).inspect
      end
    end

    # ------------------------------------------------------------------ plan mode
    def plan_line(d)
      return unless @plan

      @plan.write("#{JSON.generate(d)}\n")
      @plan.flush
    end

    def plan_draw(node, src, op, v, is_bytes)
      d = { "t" => "draw", "node" => node, "src" => src, "op" => op }
      if is_bytes
        d["hex"] = v.b.unpack1("H*")
      else
        d["v"] = v.inspect
        d["rb"] = v.inspect
      end
      plan_line(d)
    end

    def plan_param(p, raw, form)
      d = { "kind" => p.kind, "size" => p.size }
      case p
      when BufParam
        d["decoded"] = form == 1
        d["hex"] = p.raw_value(raw, form, self).unpack1("H*")
      when SrcParam
        d["hex"] = raw.byteslice(0, p.size).unpack1("H*")
      else
        v = p.decode(raw, form, self)
        d["signed"] = p.signed ? true : false
        d["v"] = v.inspect
        d["index"] = p.index(raw) if p.is_a?(OneOfParam)
        d["d"] = v.inspect if p.kind == "float"
      end
      d["rb"] = p.is_a?(SrcParam) || p.kind == "file" ? nil : p.decode(raw, form, self).inspect
      d["datatype"] = p.datatype if p.datatype
      d
    end

    # ------------------------------------------------------------------ program
    # Program bytes -> [[ep_name, [[src, out]], [[form, bytes]]]] or nil.
    def self.parse(data)
      return nil unless data.byteslice(0, 4) == "STCH"

      off = 4
      nn = data.byteslice(off, 4).unpack1("L<")
      off += 4
      return nil if nn.nil? || nn > 100_000

      names = Array.new(nn) do
        l = data.byteslice(off, 2).unpack1("S<")
        off += 2
        s = data.byteslice(off, l).dup.force_encoding(Encoding::UTF_8).scrub
        off += l
        s
      end
      n = data.byteslice(off, 4).unpack1("L<")
      off += 4
      return nil if n.nil? || n > MAX_NODES

      Array.new(n) do
        ni, nin = data.byteslice(off, 5).unpack("L<C")
        off += 5
        ins = Array.new(nin) do
          x = data.byteslice(off, 5).unpack("L<C")
          off += 5
          x
        end
        np = data.getbyte(off)
        off += 1
        ps = Array.new(np) do
          form, l = data.byteslice(off, 5).unpack("CL<")
          off += 5
          return nil if l.nil? || l > (16 << 20) || off + l > data.bytesize

          v = data.byteslice(off, l)
          off += l
          [form, v]
        end
        [ni < names.size ? names[ni] : "?", ins, ps]
      end
    rescue StandardError
      nil
    end

    def report(ep, exc, what = nil)
      quiet = FUZZING && !@trace
      if what
        @crash_pc = (1 << 63) | Stitch.fnv1a(what.b)
        return if quiet

        $stderr.write("\n[stitch] CHECK FAILED: #{what} (node #{@cur_node})\n")
        frames = Stitch.frames(exc)
      else
        kind = Stitch.exc_name(exc)
        frames = Stitch.frames(exc)
        harness = "#{ENV.fetch('STITCH_PROJECT', '/project')}/harness"
        top = frames.find { |_, f, _| !f.start_with?(harness) }
        top = top ? "#{top[1]}:#{top[2]}" : ""
        idx = @reg.endpoints.index(@reg.by_name[ep]) || 0
        # bucket key for the engine: endpoint + exception class + innermost library line
        @crash_pc = (1 << 63) | (idx << 32) | Stitch.fnv1a("#{kind}|#{top}".b)
        return if quiet

        msg = exc.message.to_s.gsub("\n", " ")[0, 500]
        $stderr.write("\n[stitch] EXCEPTION in #{ep}: #{kind}: #{msg}\n")
      end
      frames.first(24).each_with_index { |(func, fn, ln), k| $stderr.write("    ##{k} 0x0 in #{func} #{fn}:#{ln}\n") }
      $stderr.write("\n")
      $stderr.flush
    end

    # Run one program. Returns :ok, :finding or :invalid.
    def run(data, trace: false)
      nodes = Runtime.parse(data)
      if nodes.nil?
        reset_results(0)
        $stderr.write("[stitch] invalid program\n") if trace
        return :invalid
      end
      reset_results(nodes.size)
      @pool = []
      @obj_of = {}
      @globals = {}
      @trace = trace
      @in_exec = true
      Instrument.begin_exec
      begin
        run_nodes(nodes)
      rescue Finding
        :finding
      ensure
        @in_exec = false
        @cur_node = -1
        Instrument.end_exec
        @tmp_files.each { |f| File.unlink(f) rescue nil } # rubocop:disable Style/RescueModifier
        @tmp_files = []
      end
    end

    def run_nodes(nodes)
      trace = @trace
      err = $stderr
      nodes.each_with_index do |(name, ins, ps), i|
        @current = i
        @cur_node = i
        Instrument.node = i
        e = @reg.by_name[name]
        if e.nil?
          @status[i] = ST_SKIP_INVALID
          plan_line({ "t" => "skip", "node" => i, "why" => "unknown endpoint" })
          err.write("[stitch] ##{i} #{name}: unknown endpoint, skipped\n") if trace
          next
        end
        @n_out[i] = e.outputs.size
        inputs = e.inputs
        params = e.params
        if ins.size != inputs.size || ps.size != params.size
          @status[i] = ST_SKIP_INVALID
          plan_line({ "t" => "skip", "node" => i, "why" => "signature mismatch" })
          err.write("[stitch] ##{i} #{name}: signature mismatch, skipped\n") if trace
          next
        end
        in_slot = []
        missing = false
        ins.each_with_index do |(src, out), k|
          si = @obj_of.fetch([src, out], -1)
          s = live(si)
          if s.nil? || !s.type.equal?(inputs[k][1].type) || in_slot.include?(si)
            missing = true
            break
          end
          in_slot << si
        end
        if missing
          @status[i] = ST_SKIP_MISSING
          plan_line({ "t" => "skip", "node" => i, "why" => "input not available" })
          err.write("[stitch] ##{i} #{name}: input not available, skipped\n") if trace
          next
        end
        if trace
          oi = pi = 0
          parts = e.args.map do |an, kind, spec|
            if kind == :obj
              src, out = ins[oi]
              oi += 1
              "#{an}=#{spec.mode == 'take' ? 'take ' : ''}o#{src}.#{out}"
            else
              form, raw = ps[pi]
              pi += 1
              "#{an}=#{describe(spec, raw, form)}"
            end
          end
          err.write("[stitch] ##{i} #{name}(#{parts.join(', ')})\n")
        end
        if @plan
          plan_call(i, e, ins, ps)
          @plan_src_next = 0
        end
        @cur_in_slots = in_slot
        @cur_consumed = 0
        @last_miss = 0
        @pending = []
        bail_reason = nil
        result = nil
        begin
          oi = pi = 0
          args = e.args.map do |_, kind, spec|
            if kind == :obj
              v = @pool[in_slot[oi]].value
              oi += 1
              v
            else
              form, raw = ps[pi]
              pi += 1
              spec.decode(raw, form, self)
            end
          end
          Instrument.in_stub = true
          begin
            result = e.fn.call(*args)
          ensure
            Instrument.in_stub = false
          end
          @status[i] = ST_OK
        rescue Bail => b
          @status[i] = ST_BAIL
          bail_reason = b.reason
        rescue CheckFailed => c
          report(name, c, c.what)
          raise Finding
        rescue Interrupt, SignalException
          raise
        rescue Exception => ex # rubocop:disable Lint/RescueException
          if e.bails_on?(ex)
            @status[i] = ST_BAIL
            bail_reason = "raised #{Stitch.exc_name(ex)}"
          elsif @exc_bail && ex.is_a?(StandardError)
            @status[i] = ST_BAIL
            bail_reason = "exception"
          else
            report(name, ex)
            raise Finding
          end
        end
        # taken / consumed inputs leave the pool
        in_slot.each_with_index do |si, k|
          take = inputs[k][1].mode == "take"
          next unless take || @cur_consumed[k] == 1

          @pool[si].alive = false
          @pool[si].value = nil
          @consumed[i] |= 1 << k unless take
        end
        if @status[i] == ST_OK && !e.outputs.empty?
          vals = e.multi ? result : [result]
          if !vals.is_a?(Array) || vals.size != e.outputs.size
            @status[i] = ST_BAIL
            bail_reason = "returned #{result.class}, expected #{e.outputs.size} outputs"
          else
            vals.each_with_index do |v, k|
              next if v.nil?

              s = Slot.new(e.outputs[k][0], v, true, @gen_counter, i, k, {})
              @gen_counter += 1
              @pending.each { |o, key, val| s.attrs[key] = val if o.equal?(v) }
              @pool << s
              @obj_of[[i, k]] = @pool.size - 1
              @out_mask[i] |= 1 << k
            end
            if e.outputs.each_with_index.any? { |(_, opt), k| !opt && @out_mask[i][k].zero? }
              @status[i] = ST_BAIL
              bail_reason = "returned nil"
            end
          end
        end
        if @status[i] == ST_BAIL
          @reason[i] = intern(bail_reason || "bail")
          @miss[i] = @last_miss
        end
        if @plan
          d = { "t" => "done", "node" => i, "status" => @status[i] == ST_OK ? "ok" : "bail",
                "outs" => (0...e.outputs.size).select { |k| @out_mask[i][k] == 1 },
                "consumed" => (0...in_slot.size).select { |k| @consumed[i][k] == 1 } }
          d["reason"] = bail_reason || "bail" if @status[i] == ST_BAIL
          plan_line(d)
        end
        next unless trace

        if @status[i] == ST_OK
          outs = (0...e.outputs.size).select { |k| @out_mask[i][k] == 1 }.map { |k| " o#{i}.#{k}" }.join
          err.write("[stitch]     -> ok#{outs.empty? ? '' : " produced#{outs}"}\n")
        else
          err.write("[stitch]     -> bail \"#{bail_reason || 'bail'}\"\n")
        end
      end
      # drop leftovers, newest first (values before the contexts they depend on)
      @current = nodes.size
      @cur_node = nodes.size
      Instrument.node = nodes.size
      leftovers = @pool.reverse.select { |s| s.alive && s.type.drop }
      plan_line({ "t" => "drops", "objs" => leftovers.map { |s| [s.node, s.out] } }) if @plan
      leftovers.each do |s|
        s.type.drop.call(s.value)
      rescue Exception # rubocop:disable Lint/RescueException
        nil
      end
      @pool = []
      @current = -1
      plan_line({ "t" => "end" })
      :ok
    end

    def plan_call(i, e, ins, ps)
      oi = pi = 0
      args = e.args.map do |an, kind, spec|
        if kind == :obj
          src, out = ins[oi]
          oi += 1
          { "obj" => [src, out], "type" => spec.type.name, "take" => spec.mode == "take", "name" => an }
        else
          form, raw = ps[pi]
          pi += 1
          plan_param(spec, raw, form).merge("name" => an)
        end
      end
      plan_line({ "t" => "call", "node" => i, "ep" => e.name, "args" => args, "outs" => e.outputs.map { |t, _| t.name },
                  "multi" => e.multi })
    end
  end

  def self.exc_name(ex) = ex.class.name || ex.class.inspect

  # Innermost-first [func, file, line], without STITCH runtime frames.
  def self.frames(ex)
    (ex.backtrace_locations || []).filter_map do |l|
      path = l.absolute_path || l.path
      next if path.nil? || path.start_with?(RUNTIME_DIR)

      [l.label.to_s, path, l.lineno]
    end
  end

  RT = Runtime.new
end
