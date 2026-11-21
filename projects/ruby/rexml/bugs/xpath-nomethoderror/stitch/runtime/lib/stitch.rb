# frozen_string_literal: true

# STITCH harness API for Ruby.
#
#   require "stitch"
#   include Stitch::DSL
#
#   Doc = Stitch.object("Doc")
#   YamlText = Stitch.datatype("YamlText")
#
#   Stitch.endpoint :parse, { text: Str[4096, YamlText] }, New[Doc], bails_on: [Psych::SyntaxError] do |text|
#     Psych.parse(text)
#   end
#
#   Stitch.endpoint :set_key, { doc: Borrow[Doc], key: Str[32], value: I32 } do |doc, key, value|
#     doc[key] = value
#   end
#
# An exception a stub does not declare in `bails_on:` is a finding.
# Reference: `stitch guide ruby`.
require_relative "stitch/spec"
require_relative "stitch/source"
require_relative "stitch/instrument"
require_relative "stitch/runtime"

module Stitch
  class << self
    # Register a stub. `specs`: { name: spec } in argument order; `returns`: New[T], New[T] | nil,
    # an Array of those, or nil. `bails_on`: exception classes that are not bugs.
    def endpoint(name, specs = {}, returns = nil, bails_on: [], &fn)
      raise ArgumentError, "Stitch.endpoint #{name}: needs a block" unless fn

      REG.add_endpoint(Endpoint.new(name, specs, returns, bails_on, fn))
      name
    end

    # Declare an object type. `drop`: called with objects left over at the end of a program.
    def object(name, drop: nil, &blk) = REG.object(name.to_s, drop || blk)

    # Declare a data type: harness/data/<name>/ holds seeds and *.dict tokens.
    def datatype(name) = REG.datatype(Stitch.name_of(name))

    # Register a decoder for a data type: the block gets a Stitch::Source, returns a String.
    def decoder(dt, &fn)
      datatype(dt).decoder = fn
    end

    # Run the block once per worker process, after the harness is loaded.
    def init(&fn) = REG.inits << fn

    # Stop this call; not a bug. The program continues.
    def bail(reason = "bail") = raise(Bail, reason)

    # Bail unless cond holds. (Stitch.require; Kernel#require is untouched.)
    def require(cond, reason = "requirement failed")
      raise Bail, reason unless cond
    end

    # An oracle: a false condition is reported as a bug ("check failed: what").
    def check(cond, what = "check")
      raise CheckFailed, what unless cond
    end

    # Printed by `stitch exec --trace`.
    def log(msg)
      $stderr.write("[stitch]     log: #{msg}\n") if RT.trace
    end

    # The library took ownership of a borrowed input in this call.
    def consume(obj) = RT.consume(obj)

    # Attach state to an object: numbers, strings, other objects (links die with them), anything.
    def set(obj, key, value) = RT.attr_set(obj, key, value)

    # Read an attribute. Without a default, a missing key is reported by `stitch probe`.
    def get(obj, key, default = MISSING) = RT.attr_get(obj, key, default)

    # A linked object, or nil if it is missing or was consumed.
    def get_obj(obj, key) = RT.attr_get(obj, key, nil)
    def has?(obj, key) = RT.attr_has(obj, key)
    def delete(obj, key) = RT.attr_del(obj, key)
    def global_set(key, value) = (RT.globals[key] = value)

    def global_get(key, default = MISSING)
      return RT.globals[key] if RT.globals.key?(key)

      if default.equal?(MISSING)
        RT.note_miss(key)
        return nil
      end
      default
    end

    # Stitch.time_limit(0.5) { ... } bails ("time limit") if the block runs longer: for library
    # calls that are slow by design, instead of hanging the worker until the engine kills it.
    def time_limit(seconds)
      Kernel.require "timeout"
      Timeout.timeout(seconds, Bail, "time limit") { yield }
    end
  end
end
