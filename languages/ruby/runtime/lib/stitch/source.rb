# frozen_string_literal: true

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
  class Source
    INTERESTING = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096,
                   0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff].freeze
    M64 = 0xFFFFFFFFFFFFFFFF

    attr_reader :buf
    attr_accessor :log # plan mode: ->(op, value, is_bytes)

    def initialize(buf = "".b, cap: nil, gen: false, seed: 0, dict: nil, spans: nil)
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
    end

    # ---------------------------------------------------------------- low level
    def rnd = @rng.rand(M64 + 1)
    def unit = @rng.rand

    def draw(n)
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
      b = draw(width) do
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
      raw = draw(w) do
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
      raw = draw(8) do
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

    def f32 = logged("f32", [uint(4)].pack("L<").unpack1("e"))
    def f64 = logged("f64", [uint(8)].pack("Q<").unpack1("E"))
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
      b = draw(1) do
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
      return logged("more", false) if @exhausted && !@gen

      b = draw(1) do
        fill = @cap.zero? ? 1.0 : @buf.bytesize.to_f / @cap
        p = fill > 0.5 ? 0.15 : 0.65
        [(rnd & 0xfe) | (unit < p ? 1 : 0)].pack("C")
      end
      logged("more", b.getbyte(0).odd? && !@exhausted)
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
