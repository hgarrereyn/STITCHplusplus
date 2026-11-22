# frozen_string_literal: true

require "zlib"

module Stitch
  # Stitch::Source: decisions inside decoders and stubs, read from a fuzzer-owned choice buffer.
  #
  # Same encoding as the C++ runtime (languages/cpp/runtime/src/runtime.cpp), so the engine's
  # mutations (string spans, dictionary tokens) work the same way:
  #   integers  little endian, fixed width
  #   range     8 bytes; out-of-range values wrap into [lo, hi]
  #   coin(p)   1 byte b; true if b < p*256
  #   more      1 byte; low bit
  #   str/bytes length prefix (1, 2 or 4 bytes, depending on max) + content
  #   f64(lo, hi) 8 bytes; the value itself when finite and in range, other bit patterns map into it
  #   fixed(n)  n bytes;  rest  every byte left
  class Source
    INTERESTING = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096,
                   0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff].freeze
    M64 = 0xFFFFFFFFFFFFFFFF

    attr_reader :buf
    attr_accessor :log # plan mode: ->(op, value, is_bytes)

    def initialize(buf = "".b, cap: nil, gen: false, seed: 0, dict: nil, spans: nil, items: nil)
      @buf = buf.b.dup
      @pos = 0
      @cap = [cap || @buf.bytesize, @buf.bytesize].max
      @gen = gen
      @rng = gen ? Random.new(seed) : nil
      @dict = dict || []
      @spans = spans
      @exhausted = false
      @log = nil
      @depth = 0
      # loop iterations for the engine: site, instance, start, end per more iteration
      @items = items
      @open = {}
      @next_inst = 0
    end

    # ---------------------------------------------------------------- low level
    def rnd = @rng.rand(M64 + 1)
    def unit = @rng.rand

    def take_bytes(n)
      if @pos + n <= @buf.bytesize
        out = @buf.byteslice(@pos, n)
        @pos += n
        return out
      end
      if @gen && @buf.bytesize + n <= @cap
        @buf = @buf.byteslice(0, @pos)
        fresh = yield.b.byteslice(0, n).ljust(n, "\0")
        @buf << fresh
        @pos += n
        return fresh
      end
      out = (@buf.byteslice(@pos, @buf.bytesize - @pos) || "".b).ljust(n, "\0")
      @pos = @buf.bytesize
      @exhausted = true
      out
    end

    def interesting(width)
      mask = (1 << (8 * width)) - 1
      case rnd % 4
      when 0 then mask
      when 1 then mask >> 1
      else INTERESTING[rnd % INTERESTING.size] & mask
      end
    end

    def le_bytes(v, width) = [v & M64].pack("Q<").byteslice(0, width)

    def uint(width)
      b = take_bytes(width) do
        x = unit < 0.3 ? interesting(width) : rnd
        le_bytes(x & ((1 << (8 * width)) - 1), width)
      end
      b.ljust(8, "\0").unpack1("Q<")
    end

    def self.len_prefix(mx) = mx < 256 ? 1 : (mx < 65_536 ? 2 : 4)

    def synth_len(mx)
      u = unit
      l = if u < 0.7 then rnd % 17
          elsif u < 0.95 then rnd % 65
          else rnd % (mx + 1)
          end
      [l, mx].min
    end

    def blob(mx, use_dict)
      w = Source.len_prefix(mx)
      p0 = @pos
      out = blob_inner(mx, use_dict, w)
      @spans.push(p0, w, out.bytesize) if @spans && p0 + w <= @buf.bytesize
      out
    end

    def blob_inner(mx, use_dict, w)
      content = nil
      raw = take_bytes(w) do
        content = if use_dict && !@dict.empty? && unit < 0.5
                    @dict[rnd % @dict.size].b.byteslice(0, mx)
                  else
                    l = synth_len(mx)
                    if unit < 0.7
                      Array.new(l) { 32 + rnd % 95 }.pack("C*")
                    else
                      Array.new(l) { rnd & 0xff }.pack("C*")
                    end
                  end
        le_bytes(content.bytesize, w)
      end
      raw = raw.ljust(8, "\0").unpack1("Q<")
      ln = raw <= mx ? raw : raw % (mx + 1)
      if content
        c = content.byteslice(0, ln)
        if @buf.bytesize + c.bytesize <= @cap
          @buf << c
          @pos += c.bytesize
          return c
        end
        @exhausted = true
        return "".b
      end
      avail = [@buf.bytesize - @pos, 0].max
      n = [ln, avail].min
      out = @buf.byteslice(@pos, n)
      @pos += n
      @exhausted = true if n < ln
      out
    end

    def range_raw(lo, hi)
      lo, hi = hi, lo if hi < lo
      span = (hi - lo + 1) & M64
      raw = take_bytes(8) do
        u = unit
        v = if u < 0.05 then lo
            elsif u < 0.1 then hi
            else lo + (span.zero? ? rnd : rnd % span)
            end
        [v].pack("q<")
      end
      raw = raw.unpack1("q<")
      return raw if raw.between?(lo, hi)

      off = (raw - lo) & M64
      lo + (span.zero? ? off : off % span)
    end

    def logged(op, value, is_bytes = false)
      @log&.call(op, value, is_bytes) if @depth.zero?
      value
    end

    # ---------------------------------------------------------------- public draws
    def u8 = logged("u8", uint(1))
    def u16 = logged("u16", uint(2))
    def u32 = logged("u32", uint(4))
    def u64 = logged("u64", uint(8))
    def i8 = logged("i8", signed(1))
    def i16 = logged("i16", signed(2))
    def i32 = logged("i32", signed(4))
    def i64 = logged("i64", signed(8))

    def signed(w)
      v = uint(w)
      (v >> (8 * w - 1)) == 1 ? v - (1 << (8 * w)) : v
    end

    def range(lo, hi) = logged("range(#{lo}, #{hi})", range_raw(lo, hi))
    def choice(n) = logged("choice(#{n})", n <= 0 ? 0 : range_raw(0, n - 1))

    def pick(items)
      items = items.to_a
      raise IndexError, "pick from an empty list" if items.empty?

      @depth += 1
      begin
        i = range_raw(0, items.size - 1)
      ensure
        @depth -= 1
      end
      logged("pick(#{items.size} items) -> ##{i}", items[i])
    end

    def coin(p = 0.5)
      thr = p * 256.0
      b = take_bytes(1) do
        yes = unit < p
        r = rnd
        if yes
          [thr >= 1 ? (r % [256.0, thr].min.to_i) : 0].pack("C")
        else
          [[255.0, thr + (r % [1.0, 256.0 - thr].max.to_i)].min.to_i].pack("C")
        end
      end
      logged("coin(#{p})", b.getbyte(0) < thr && !@exhausted)
    end

    def more
      at = [@pos, @buf.bytesize].min
      v = false
      unless @exhausted && !@gen
        b = take_bytes(1) do
          fill = @cap.zero? ? 1.0 : @buf.bytesize.to_f / @cap
          p = fill > 0.5 ? 0.15 : 0.65
          [(rnd & 0xfe) | (unit < p ? 1 : 0)].pack("C")
        end
        v = b.getbyte(0).odd? && !@exhausted
      end
      if @items
        # the loop: its source line, and the stack depth (recursion level)
        loc = caller_locations(1, 1).first
        site = Zlib.crc32("#{loc&.path}:#{loc&.lineno}")
        loop_item(site, caller_locations.size, at, v)
      end
      logged("more", v)
    end

    def loop_item(site, depth, p, v)
      st = (@open[[site, depth]] ||= [(@next_inst += 1) - 1, -1, site])
      @items.push(site, st[0], st[1], p) if st[1] >= 0
      if v
        st[1] = p
      else
        @items.push(site, st[0], p, p)
        @open.delete([site, depth])
      end
    end

    def finish_items
      n = @buf.bytesize
      @open.each_value do |inst, start, site|
        @items.push(site, inst, start, n) if start >= 0
        @items.push(site, inst, n, n)
      end
      @open = {}
    end

    # f64: any double. f64(lo, hi): a finite double in [lo, hi].
    def f64(lo = nil, hi = nil)
      return logged("f64", [uint(8)].pack("Q<").unpack1("E")) if lo.nil?

      lo, hi = hi, lo if hi < lo
      lo = lo.to_f
      hi = hi.to_f
      bits = take_bytes(8) do
        u = unit
        x = if u < 0.08 then lo
            elsif u < 0.16 then hi
            elsif u < 0.22 && lo <= 0 && hi >= 0 then 0.0
            else lo + (hi - lo) * unit
            end
        x = lo unless x.between?(lo, hi)
        [x].pack("E")
      end.unpack1("Q<")
      v = [bits].pack("Q<").unpack1("E")
      unless v.finite? && v.between?(lo, hi)
        p = (bits >> 11) / 9_007_199_254_740_992.0
        w = hi - lo
        v = if w.finite? then lo + w * p
            elsif p < 0.5 then lo + (hi / 2 - lo / 2) * (2 * p)
            else lo / 2 + hi / 2 + (hi / 2 - lo / 2) * (2 * p - 1)
            end
        v = v.clamp(lo, hi)
      end
      logged("f64(#{lo}, #{hi})", v)
    end

    def f32(lo = nil, hi = nil)
      return logged("f32", [uint(4)].pack("L<").unpack1("e")) if lo.nil?

      lo, hi = hi, lo if hi < lo
      lo = lo.to_f
      hi = hi.to_f
      bits = take_bytes(4) do
        u = unit
        x = u < 0.08 ? lo : (u < 0.16 ? hi : lo + (hi - lo) * unit)
        x = [x].pack("e").unpack1("e")
        x = lo unless x.between?(lo, hi)
        [x].pack("e")
      end.unpack1("L<")
      v = [bits].pack("L<").unpack1("e")
      unless v.finite? && v.between?(lo, hi)
        p = (bits >> 8) / 16_777_216.0
        v = [(lo + (hi - lo) * p).clamp(lo, hi)].pack("e").unpack1("e")
      end
      logged("f32(#{lo}, #{hi})", v)
    end

    # Exactly n bytes (zeros past the end of the input).
    def fixed(n)
      return logged("fixed(#{n})", "".b, true) if n <= 0

      v = take_bytes(n) do
        pr = unit < 0.5
        Array.new(n) { pr ? 32 + rnd % 95 : rnd & 0xff }.pack("C*")
      end
      logged("fixed(#{n})", v, true)
    end

    # Every byte that is left.
    def rest
      p0 = @pos
      if @gen && @pos >= @buf.bytesize && @buf.bytesize < @cap
        room = @cap - @buf.bytesize
        l = [synth_len(room), room].min
        pr = unit < 0.7
        @buf = @buf.byteslice(0, @pos)
        @buf << Array.new(l) { pr ? 32 + rnd % 95 : rnd & 0xff }.pack("C*")
      end
      v = @buf.byteslice([@pos, @buf.bytesize].min..) || "".b
      @pos = @buf.bytesize
      @spans&.push(p0, 0, v.bytesize) # no length prefix
      logged("rest", v, true)
    end

    # A value from data type dt's decoder, drawn from this source: decoders build on each other.
    def draw(dt)
      fn = dt.respond_to?(:decoder) ? dt.decoder : nil
      raise TypeError, "#{dt.inspect} has no decoder" unless fn

      fn.call(self)
    end

    def bytes(max = 64) = logged("bytes(#{max})", blob(max, false), true)

    def str(max = 64)
      v = blob(max, false)
      logged("str(#{max})", v, true)
      v.dup.force_encoding(Encoding::UTF_8).scrub
    end

    def token
      v = blob(255, true)
      logged("token", v, true)
      v.dup.force_encoding(Encoding::UTF_8).scrub
    end

    # ---------------------------------------------------------------- state
    def position = @pos
    def remaining = [@buf.bytesize - @pos, 0].max
    def exhausted? = @exhausted
  end
end
