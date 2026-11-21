# frozen_string_literal: true

# Signature vocabulary: what a stub's argument specs mean.
#
#   Stitch.endpoint :set, { doc: Borrow[Doc], key: Str[64], n: Int[0, 9] }, New[Item] do |doc, key, n| ... end
#
# Inputs are objects (Borrow / Take). Parameters are fuzzed values; their byte encoding matches the
# C++ runtime, so the engine mutates them the same way.
module Stitch
  # An object type: a name for values that stubs produce and consume.
  class ObjectType
    attr_reader :name
    attr_accessor :drop

    def initialize(name, drop = nil)
      @name = name
      @drop = drop
    end

    def inspect = "#<stitch object #{@name}>"
  end

  # A data type: a name for a kind of fuzzed value (seeds, dictionary, decoder).
  class Datatype
    attr_reader :name
    attr_accessor :decoder

    def initialize(name)
      @name = name
      @decoder = nil
    end

    def inspect = "#<stitch datatype #{@name}>"
  end

  def self.name_of(x)
    case x
    when nil then nil
    when Datatype, ObjectType then x.name
    when String, Symbol then x.to_s
    else raise TypeError, "expected a Stitch.datatype(...) or a name, got #{x.inspect}"
    end
  end

  # ------------------------------------------------------------------------------ objects
  class ObjSpec
    attr_reader :type, :mode

    def initialize(type, mode)
      raise TypeError, "Borrow/Take/New take an object type from Stitch.object, got #{type.inspect}" unless type.is_a?(ObjectType)

      @type = type
      @mode = mode
    end
  end

  class NewSpec
    attr_reader :type, :optional

    def initialize(type, optional = false)
      raise TypeError, "New[...] takes an object type from Stitch.object, got #{type.inspect}" unless type.is_a?(ObjectType)

      @type = type
      @optional = optional
    end

    # New[T] | nil: an optional output
    def |(other)
      raise TypeError, "New[T] | nil is the only union" unless other.nil?

      NewSpec.new(@type, true)
    end
  end

  # Borrow[T]: uses an object; it stays available. `Stitch.consume(x)` if the library took it.
  module Borrow
    def self.[](type) = ObjSpec.new(type, "borrow")
  end

  # Take[T]: consumes the object.
  module Take
    def self.[](type) = ObjSpec.new(type, "take")
  end

  # New[T]: the stub returns a new object. `New[T] | nil`: optional output.
  module New
    def self.[](type) = NewSpec.new(type)
  end

  # ------------------------------------------------------------------------------ params
  class Param
    attr_reader :kind, :ctype, :size, :signed, :lo, :hi, :values, :datatype

    def manifest(name)
      d = { "kind" => kind, "type" => ctype, "size" => size, "signed" => signed ? true : false,
            "datatype" => datatype, "name" => name.to_s }
      if kind == "range"
        d["lo"] = lo
        d["hi"] = hi
      end
      d["values"] = values.to_a if values && !values.empty?
      d
    end

    def self.le_int(raw, size, signed)
      b = raw.byteslice(0, size).b.ljust(size, "\0")
      v = b.unpack1("Q<") if size == 8
      v ||= b.bytes.each_with_index.sum { |c, i| c << (8 * i) }
      v -= 1 << (8 * size) if signed && v >> (8 * size - 1) == 1
      v
    end
  end

  class IntParam < Param
    def initialize(size, signed, ctype)
      @kind = "int"
      @size = size
      @signed = signed
      @ctype = ctype
    end

    def decode(raw, _form, _rt) = Param.le_int(raw, @size, @signed)
  end

  class FloatParam < Param
    def initialize(size)
      @kind = "float"
      @size = size
      @ctype = size == 4 ? "F32" : "F64"
    end

    def decode(raw, _form, _rt)
      b = raw.byteslice(0, @size).b.ljust(@size, "\0")
      b.unpack1(@size == 4 ? "e" : "E")
    end
  end

  class BoolParam < Param
    def initialize
      @kind = "bool"
      @ctype = "Bool"
      @size = 1
    end

    def decode(raw, _form, _rt) = !raw.empty? && raw.getbyte(0).odd?
  end

  class RangeParam < Param
    def initialize(lo, hi)
      raise ArgumentError, "Range[lo, hi] needs lo <= hi" if lo > hi

      @kind = "range"
      @ctype = "Range"
      @size = 8
      @signed = true
      @lo = Integer(lo)
      @hi = Integer(hi)
    end

    def decode(raw, _form, _rt)
      v = Param.le_int(raw, 8, true)
      return v if v.between?(@lo, @hi)

      span = (@hi - @lo + 1) & 0xFFFFFFFFFFFFFFFF
      off = (v - @lo) & 0xFFFFFFFFFFFFFFFF
      @lo + (span.zero? ? off : off % span)
    end
  end

  # One of the listed values (any Ruby objects). The engine sees indices.
  class OneOfParam < Param
    attr_reader :choices

    def initialize(choices)
      raise ArgumentError, "OneOf[...] needs at least one value" if choices.empty?

      @kind = "oneof"
      @ctype = "OneOf"
      @size = 8
      @choices = choices
      @values = (0...choices.size).to_a
    end

    def index(raw)
      v = Param.le_int(raw, 8, true)
      v >= 0 && v < @choices.size ? v : (v & 0xFFFFFFFFFFFFFFFF) % @choices.size
    end

    def decode(raw, _form, _rt) = @choices[index(raw)]
  end

  class FlagsParam < Param
    def initialize(flags)
      @kind = "flags"
      @ctype = "Flags"
      @size = 8
      @values = flags.map { |f| Integer(f) }
      @mask = @values.reduce(0, :|)
    end

    def decode(raw, _form, _rt) = Param.le_int(raw, 8, true) & @mask
  end

  # Bytes / Str / File: a buffer of at most `size` bytes, optionally of a data type.
  class BufParam < Param
    def initialize(kind, size, datatype = nil)
      @kind = kind
      @ctype = { "bytes" => "Bytes", "str" => "Str", "file" => "File" }.fetch(kind)
      @size = Integer(size)
      @datatype = Stitch.name_of(datatype)
    end

    def raw_value(raw, form, rt) = rt.decode_param(@datatype, raw, form, @size)

    def decode(raw, form, rt)
      b = raw_value(raw, form, rt)
      case @kind
      when "bytes" then b.b
      when "str" then b.dup.force_encoding(Encoding::UTF_8).scrub
      else rt.temp_file(b)
      end
    end
  end

  class SrcParam < Param
    def initialize(size)
      @kind = "source"
      @ctype = "Source"
      @size = Integer(size)
    end

    def decode(raw, _form, rt) = rt.new_source(raw.byteslice(0, @size).b, @size)
  end

  def self.sized(kind, args, default)
    size = default
    dt = nil
    args.each { |a| a.is_a?(Integer) ? size = a : dt = a }
    BufParam.new(kind, size, dt)
  end

  # Bytes[max, Tag]: a binary String of at most max bytes.
  module Bytes
    def self.[](*args) = Stitch.sized("bytes", args, 256)
  end

  # Str[max, Tag]: a UTF-8 String (invalid bytes replaced).
  module Str
    def self.[](*args) = Stitch.sized("str", args, 256)
  end

  # FilePath[max, Tag]: path of a temporary file holding the data (valid during the call).
  # (Not `File`: a top-level File[...] would find ::File first.)
  module FilePath
    def self.[](*args) = Stitch.sized("file", args, 4096)
  end

  # Int[lo, hi]: an Integer in lo..hi. (Not `Range`, for the same reason.)
  module Int
    def self.[](lo, hi) = RangeParam.new(lo, hi)
  end

  # OneOf[a, b, c]: one of the listed values.
  module OneOf
    def self.[](*args) = OneOfParam.new(args)
  end

  # Flags[A, B, C]: bitwise OR of a subset of the flags.
  module Flags
    def self.[](*args) = FlagsParam.new(args)
  end

  # Src[max]: a Stitch::Source for choices made inside the stub.
  module Src
    def self.[](size) = SrcParam.new(size)
  end

  U8 = IntParam.new(1, false, "U8")
  U16 = IntParam.new(2, false, "U16")
  U32 = IntParam.new(4, false, "U32")
  U64 = IntParam.new(8, false, "U64")
  I8 = IntParam.new(1, true, "I8")
  I16 = IntParam.new(2, true, "I16")
  I32 = IntParam.new(4, true, "I32")
  I64 = IntParam.new(8, true, "I64")
  F32 = FloatParam.new(4)
  F64 = FloatParam.new(8)
  Bool = BoolParam.new

  # spec -> [:obj, ObjSpec] | [:param, Param]
  def self.resolve(spec, where)
    case spec
    when ObjSpec then [:obj, spec]
    when NewSpec then raise TypeError, "#{where}: New[...] is for outputs"
    when ObjectType then [:obj, ObjSpec.new(spec, "borrow")]
    when Param then [:param, spec]
    else
      plain = { ::Integer => I64, ::Float => F64, ::String => BufParam.new("str", 256) }
      return [:param, plain[spec]] if plain.key?(spec)

      raise TypeError, "#{where}: unsupported spec #{spec.inspect} (use Borrow[T], Take[T], Str[n], Bytes[n], I32, Int[a, b], ...)"
    end
  end

  # `include Stitch::DSL` (at the top of a harness file) brings the vocabulary into scope.
  module DSL
    Borrow = Stitch::Borrow
    Take = Stitch::Take
    New = Stitch::New
    Str = Stitch::Str
    Bytes = Stitch::Bytes
    FilePath = Stitch::FilePath
    Int = Stitch::Int
    OneOf = Stitch::OneOf
    Flags = Stitch::Flags
    Src = Stitch::Src
    Bool = Stitch::Bool
    U8 = Stitch::U8
    U16 = Stitch::U16
    U32 = Stitch::U32
    U64 = Stitch::U64
    I8 = Stitch::I8
    I16 = Stitch::I16
    I32 = Stitch::I32
    I64 = Stitch::I64
    F32 = Stitch::F32
    F64 = Stitch::F64
  end
end
