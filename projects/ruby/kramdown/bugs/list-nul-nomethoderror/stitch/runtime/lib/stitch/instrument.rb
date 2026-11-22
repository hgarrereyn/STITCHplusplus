# frozen_string_literal: true

# Coverage counters and comparison logging for the code under test (the Jazzer.js approach, with
# Prism instead of Babel).
#
# Target files are compiled from rewritten source through RubyVM::InstructionSequence.load_iseq
# (the hook bootsnap uses):
#   the start of every statement list (method, block and branch bodies, when/in/rescue/else
#   clauses, loop bodies) and the right side of && / ||  ->  ::STITCH_COV.setbyte(ID, ...)
#   a == b, !=, <, <=, >, >=, eql?, start_with?, end_with?, include?  ->  Instrument.cmp / .call1
#   case x when "lit" ...  ->  case Instrument.sw(x, ["lit", ...])
# Nothing adds a line, so backtraces keep their line numbers. IDs hash (file, offset, tag) and
# are stable across processes. The map is a binary String: counters are single bytes that never
# return to 0, and collecting them is a C-level scan.
#
# Targets: STITCH_RB_INCLUDE (comma-separated gem names or path prefixes) and the harness; without
# it, every file outside the Ruby standard library and the STITCH++ runtime. Only while fuzzing
# (STITCH_FUZZING=1).
module Stitch
  module Instrument
    MAP_SIZE = 1 << 16
    MASK = MAP_SIZE - 1
    ZERO = ("\0" * MAP_SIZE).b.freeze
    CHUNK = 256
    ZERO_CHUNK = ("\0" * CHUNK).b.freeze
    NONZERO_BYTES = "\x01-\xff".b.freeze
    ONE = "\x01".b.freeze
    OPS = %i[== != < <= > >= eql?].freeze
    CALLS = %i[start_with? end_with? include? key? has_key? member?].freeze

    @cmplog = false
    @in_stub = false
    @busy = false
    @node = 0
    @cmps = []
    @seen = {}
    @active = false

    class << self
      attr_accessor :node, :cmplog
      attr_reader :active

      def in_stub=(v)
        @in_stub = v
      end

      # ------------------------------------------------------------ per program
      def begin_exec
        @cmps.clear
        @seen.clear
      end

      def end_exec
        @cmplog = false
      end

      # [count][index u32, count u8]* for the counters this program hit; clears them. Two levels
      # of C-level string operations: 256-byte chunks compared with a zero chunk (memcmp), then,
      # in the few non-zero chunks, `tr` maps every non-zero byte to 1 and `index` finds them.
      def edges_bytes
        cov = ::STITCH_COV
        out = +"".b
        n = 0
        i = 0
        while i < MAP_SIZE
          if cov.byteslice(i, CHUNK) != ZERO_CHUNK
            marks = cov.byteslice(i, CHUNK).tr(NONZERO_BYTES, ONE)
            pos = 0
            while (k = marks.index(ONE, pos))
              out << [i + k, cov.getbyte(i + k)].pack("L<C")
              n += 1
              pos = k + 1
            end
          end
          i += CHUNK
        end
        cov.replace(ZERO) if n.positive?
        [n].pack("L<") << out
      end

      def cmps_bytes = [@cmps.size].pack("L<") << @cmps.join

      # ------------------------------------------------------------ comparison hooks
      def cmp(a, b, op)
        r = case op
            when 0 then a == b
            when 1 then a != b
            when 2 then a < b
            when 3 then a <= b
            when 4 then a > b
            when 5 then a >= b
            else a.eql?(b)
            end
        record(a, b) if @cmplog && @in_stub && !@busy
        r
      end

      def call1(recv, name, arg)
        r = recv.__send__(name, arg)
        if @cmplog && @in_stub && !@busy && !r
          if recv.is_a?(String) || recv.is_a?(Symbol)
            record(recv, arg)
          elsif (recv.is_a?(Hash) || recv.is_a?(Array) || recv.is_a?(Set)) && recv.size <= 256
            keys = recv.is_a?(Hash) ? recv.keys : recv.to_a
            keys.first(64).each { |k| record(arg, k) if k.instance_of?(arg.class) }
          end
        end
        r
      end

      def sw(x, lits)
        lits.each { |l| record(x, l) } if @cmplog && @in_stub && !@busy
        x
      end

      def record(a, b)
        @busy = true
        if a.is_a?(Integer) && b.is_a?(Integer)
          rec_int(a, b) if a != b
        else
          ba = as_bytes(a)
          bb = as_bytes(b)
          rec_bytes(ba, bb) if ba && bb && ba != bb
        end
      rescue StandardError
        nil
      ensure
        @busy = false
      end

      def as_bytes(v)
        case v
        when String then v.b
        when Symbol then v.to_s.b
        end
      end

      def rec_int(a, b)
        a &= 0xFFFFFFFFFFFFFFFF
        b &= 0xFFFFFFFFFFFFFFFF
        k = [@node, a, b]
        return if @cmps.size >= 16_384 || @seen.key?(k)

        @seen[k] = true
        @cmps << ([@node, 0, 8, 8, 8, 0, 0].pack("S<CCCCCC") << [a].pack("Q<").ljust(32, "\0") << [b].pack("Q<").ljust(32, "\0"))
      end

      def rec_bytes(a, b)
        a = a.byteslice(0, 32)
        b = b.byteslice(0, 32)
        return if a.empty? || b.empty?

        k = [@node, a, b]
        return if @cmps.size >= 16_384 || @seen.key?(k)

        @seen[k] = true
        @cmps << ([@node, 1, 0, a.bytesize, b.bytesize, 0, 0].pack("S<CCCCCC") << a.ljust(32, "\0") << b.ljust(32, "\0"))
      end

      # ------------------------------------------------------------ targets
      def setup
        @include = ENV.fetch("STITCH_RB_INCLUDE", "").split(",").map(&:strip).reject(&:empty?)
        @exclude = ENV.fetch("STITCH_RB_EXCLUDE", "").split(",").map(&:strip).reject(&:empty?)
        @stdlib = [RbConfig::CONFIG["rubylibdir"], RbConfig::CONFIG["rubyarchdir"]].compact.map { |d| File.realpath(d) rescue d } # rubocop:disable Style/RescueModifier
        @harness = File.join(File.realpath(ENV.fetch("STITCH_PROJECT", Dir.pwd)), "harness")
      end

      def matches?(path, pat)
        path.start_with?(pat) || path.include?("/gems/#{pat}-") || path.include?("/#{pat}/lib/")
      end

      def wanted?(path)
        return true if path.start_with?(@harness)
        return false if path.start_with?(RUNTIME_DIR)
        return false if @exclude.any? { |p| matches?(path, p) }
        return @include.any? { |p| matches?(path, p) } unless @include.empty?

        @stdlib.none? { |d| path.start_with?(d) }
      end

      # Instrument files loaded from now on. Call before loading the harness.
      def install
        return if @active

        @active = true
        setup
        require "prism"
        require "set"
        Object.const_set(:STITCH_COV, ZERO.dup) unless Object.const_defined?(:STITCH_COV)
        RubyVM::InstructionSequence.singleton_class.prepend(LoadHook)
      end

      def rewrite(src, path) = Rewriter.new(src, path).run
    end

    # RubyVM::InstructionSequence.load_iseq: compile target files from rewritten source.
    module LoadHook
      def load_iseq(path)
        real = File.realpath(path)
        return super_or_nil(path) unless Instrument.wanted?(real)

        code = Instrument.rewrite(File.binread(path), path)
        RubyVM::InstructionSequence.compile(code, real, real, 1)
      rescue SyntaxError, StandardError => e
        warn "[stitch] not instrumented: #{path}: #{e.class}: #{e.message.lines.first}" if ENV["STITCH_DEBUG"]
        super_or_nil(path)
      end

      def super_or_nil(path)
        defined?(super) ? super : nil
      end
    end

    # Source-to-source rewriting over the Prism tree. Each node's text is rebuilt from its
    # children's (rewritten) text and the original text between them; a node whose children do
    # not lie in order inside it (heredoc bodies) is copied unchanged.
    class Rewriter
      def initialize(src, path)
        @src = src.b
        @path = path
        @tree = Prism.parse(src.dup.force_encoding(Encoding::UTF_8))
      end

      def run
        return @src.dup.force_encoding(Encoding::UTF_8) unless @tree.errors.empty?

        emit(@tree.value, nil).force_encoding(Encoding::UTF_8)
      end

      def raw(a, b) = @src.byteslice(a, b - a)

      def counter(off, tag)
        id = Stitch.fnv1a("#{@path}:#{off}:#{tag}".b) & MASK
        "::STITCH_COV.setbyte(#{id}, ::STITCH_COV.getbyte(#{id}) + 1 | 1)"
      end

      def span(n) = [n.location.start_offset, n.location.end_offset]

      # rewritten text of node n (whose parent is `parent`)
      def emit(n, parent)
        s, e = span(n)
        return raw(s, e) if n.is_a?(Prism::DefinedNode) || heredoc?(n)

        kids = n.compact_child_nodes.sort_by { |c| c.location.start_offset }
        pos = s
        kids.each do |c|
          cs, ce = span(c)
          return raw(s, e) if cs < pos || ce > e

          pos = ce
        end
        texts = {}
        kids.each { |c| texts[c] = emit(c, n) }
        special = transform(n, parent, texts)
        return special if special

        out = +"".b
        pos = s
        kids.each do |c|
          cs, ce = span(c)
          out << raw(pos, cs) << texts[c]
          pos = ce
        end
        out << raw(pos, e)
        statements_counter(n, parent, out)
      end

      def heredoc?(n)
        (n.is_a?(Prism::StringNode) || n.is_a?(Prism::InterpolatedStringNode) || n.is_a?(Prism::XStringNode) ||
          n.is_a?(Prism::InterpolatedXStringNode)) && n.opening_loc&.slice&.start_with?("<<")
      end

      # comparisons and case subjects (nil: no special form)
      def transform(n, parent, texts)
        case n
        when Prism::CallNode
          return nil if n.receiver.nil? || n.block || n.safe_navigation? || n.arguments.nil?

          args = n.arguments.arguments
          return nil unless args.size == 1
          return nil if args[0].is_a?(Prism::SplatNode) || args[0].is_a?(Prism::KeywordHashNode) || args[0].is_a?(Prism::BlockArgumentNode)

          if (op = OPS.index(n.name))
            return "::Stitch::Instrument.cmp(#{texts[n.receiver]}, #{texts[n.arguments]}, #{op})".b
          end
          if CALLS.include?(n.name)
            return "::Stitch::Instrument.call1(#{texts[n.receiver]}, :#{n.name}, #{texts[n.arguments]})".b
          end

          nil
        when Prism::AndNode, Prism::OrNode
          s, e = span(n)
          ls, le = span(n.left)
          rs, re = span(n.right)
          return nil unless s <= ls && le <= rs && re <= e

          raw(s, ls) << texts[n.left] << raw(le, rs) << "(#{counter(rs, 'r')}; ".b << texts[n.right] << ")".b << raw(re, e)
        when Prism::CaseNode
          pred = n.predicate
          return nil if pred.nil?

          lits = n.conditions.flat_map { |w| w.respond_to?(:conditions) ? w.conditions : [] }.select do |c|
            c.is_a?(Prism::StringNode) || c.is_a?(Prism::SymbolNode) || c.is_a?(Prism::IntegerNode)
          end
          return nil if lits.empty? || lits.any? { |l| heredoc?(l) }

          s, e = span(n)
          ps, pe = span(pred)
          kids = n.compact_child_nodes.sort_by { |c| c.location.start_offset }
          out = +"".b
          pos = s
          kids.each do |c|
            cs, ce = span(c)
            out << raw(pos, cs)
            out << (c.equal?(pred) ? "::Stitch::Instrument.sw(#{texts[pred]}, [#{lits.map { |l| l.slice.b }.join(', ')}])".b : texts[c])
            pos = ce
          end
          return nil if ps < s || pe > e

          out << raw(pos, e)
        end
      end

      # a counter at the start of each statement list
      def statements_counter(n, parent, out)
        return out unless n.is_a?(Prism::StatementsNode)
        return out if parent.is_a?(Prism::EmbeddedStatementsNode) || parent.is_a?(Prism::ParenthesesNode)
        # `begin ... end while c` runs the body first: count inside the begin instead
        return out if (parent.is_a?(Prism::WhileNode) || parent.is_a?(Prism::UntilNode)) && parent.begin_modifier?

        c = counter(n.location.start_offset, "s#{parent.class.name}")
        if wrap?(n, parent)
          "(#{c}; ".b << out << ")".b
        else
          "#{c}; ".b << out
        end
      end

      # statement lists that are expressions in a larger construct: ternaries, modifiers, endless defs
      def wrap?(n, parent)
        case parent
        when Prism::IfNode
          parent.if_keyword_loc.nil? || parent.if_keyword_loc.start_offset > n.location.start_offset
        when Prism::UnlessNode
          parent.keyword_loc.start_offset > n.location.start_offset
        when Prism::WhileNode, Prism::UntilNode
          parent.keyword_loc.start_offset > n.location.start_offset
        when Prism::ElseNode
          parent.else_keyword_loc.slice == ":"
        when Prism::DefNode
          !parent.equal_loc.nil?
        else
          false
        end
      end
    end
  end
end
