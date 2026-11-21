# ---------------------------------------------------------------------------------------------
# Standalone stand-in for the STITCH++ runtime (written next to repro.rb by `stitch write`).
# The harness code above runs unchanged: Stitch.endpoint only records the block and bails_on,
# Stitch.bail/check and attributes work without the fuzzer, and Source replays recorded choices.
#   Stitch.call(n, :name) { ... }        a call that succeeded in the original run; a bail = DIVERGED
#   Stitch.call_bails(n, :name) { ... }  a call that bailed in the original run
# STITCH_REPRO_TRACE=1 prints one line per call.
# ---------------------------------------------------------------------------------------------
module Stitch
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

  # Spec stand-in: Str[64, T], New[Doc] | nil, Int[0, 9] ... evaluate to this.
  class AnySpec
    def self.[](*) = self
    def self.|(_) = self
  end

  module DSL
    Borrow = Take = New = Str = Bytes = FilePath = Int = OneOf = Flags = Src = AnySpec
    U8 = U16 = U32 = U64 = I8 = I16 = I32 = I64 = F32 = F64 = Bool = AnySpec
  end

  Named = Struct.new(:name)
  STUBS = {}
  BAILS_ON = {}
  ATTRS = {}.compare_by_identity
  GLOBALS = {}
  CUR = [-1, "?"]
  TRACE = !ENV["STITCH_REPRO_TRACE"].to_s.empty?
  MISSING_ = Object.new

  class << self
    def object(name, **) = Named.new(name.to_s)
    def datatype(name) = Named.new(name.to_s)
    def decoder(_dt) = nil
    def init = yield

    def endpoint(name, _specs = {}, _returns = nil, bails_on: [], &fn)
      STUBS[name.to_sym] = fn
      BAILS_ON[name.to_sym] = Array(bails_on)
      name
    end

    def stub(name) = STUBS.fetch(name)
    def bail(reason = "bail") = raise(Bail, reason)

    def require(cond, reason = "requirement failed")
      raise Bail, reason unless cond
    end

    def check(cond, what = "check")
      raise CheckFailed, what unless cond
    end

    def log(msg)
      $stderr.write("[repro]     log: #{msg}\n") if TRACE
    end

    def consume(_obj) = nil
    def set(obj, key, value) = ((ATTRS[obj] ||= {})[key] = value)
    def get(obj, key, default = nil) = (ATTRS[obj] || {}).fetch(key, default)
    def get_obj(obj, key) = get(obj, key)
    def has?(obj, key) = (ATTRS[obj] || {}).key?(key)
    def delete(obj, key) = (ATTRS[obj] || {}).delete(key)
    def global_set(key, value) = (GLOBALS[key] = value)
    def global_get(key, default = nil) = GLOBALS.fetch(key, default)
    def time_limit(_seconds) = yield

    # Temporary file holding `data` (a FilePath[...] parameter).
    def file(data)
      Kernel.require "tmpdir"
      path = File.join(Dir.tmpdir, "stitch-repro-#{Process.pid}-#{rand(1 << 30)}")
      File.binwrite(path, data)
      path
    end

    def diverged(why)
      $stderr.write("\n[repro] DIVERGED at call ##{CUR[0]} #{CUR[1]}: #{why}\n")
      exit 99
    end

    def call(n, name)
      CUR[0] = n
      CUR[1] = name.to_s
      r = begin
        yield
      rescue Bail => b
        diverged("bailed: \"#{b.reason}\" (it succeeded in the original run)")
      end
      $stderr.write("[repro] ##{n} #{name} -> ok\n") if TRACE
      r
    end

    def call_bails(n, name)
      CUR[0] = n
      CUR[1] = name.to_s
      begin
        yield
      rescue Bail
        nil
      rescue Exception => e # rubocop:disable Lint/RescueException
        raise unless BAILS_ON.fetch(name.to_sym, []).any? { |c| e.is_a?(c) }
      end
      $stderr.write("[repro] ##{n} #{name} -> bail\n") if TRACE
    end

    # Run the program; report a crash in the same format as the fuzzer.
    def run
      yield
    rescue CheckFailed => c
      $stderr.write("\n[stitch] CHECK FAILED: #{c.what} (node #{CUR[0]})\n")
      exit 1
    rescue SystemExit
      raise
    rescue Exception => e # rubocop:disable Lint/RescueException
      $stderr.write("\n[stitch] EXCEPTION in #{CUR[1]}: #{e.class.name}: #{e.message.to_s.gsub("\n", ' ')[0, 500]}\n")
      frames = (e.backtrace_locations || []).reject { |l| (l.absolute_path || l.path) == __FILE__ }
      frames.first(24).each_with_index do |l, k|
        $stderr.write("    ##{k} 0x0 in #{l.label} #{File.expand_path(l.absolute_path || l.path)}:#{l.lineno}\n")
      end
      exit 1
    end
  end
end
