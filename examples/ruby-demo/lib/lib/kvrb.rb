# frozen_string_literal: true

# A tiny key-value store with planted bugs (STITCH++ example).
module Kvrb
  class Store
    def initialize
      @d = {}
      @order = []
    end

    def put(key, value)
      raise ArgumentError, "keys and values are single non-empty lines" if key.empty? || key.include?("\n") || value.include?("\n")

      @order << key unless @d.key?(key)
      @d[key] = value
    end

    def get(key) = @d[key]

    def remove(key)
      return unless @d.key?(key)

      @d.delete(key)
      @order.delete(key) unless key.start_with?("tmp") # BUG: temporary keys stay in the order list
    end

    def items = @order.map { |k| [k, @d.fetch(k)] } # KeyError after removing a tmp key

    def merge(other) = other.items.each { |k, v| put(k, v) }

    def dump = items.map { |k, v| "#{k}=#{v}" }.join("\n")
  end

  def self.load(text)
    st = Store.new
    text.split("\n").each do |line|
      next if line.empty?
      raise ArgumentError, "line without '=': #{line.inspect}" unless line.include?("=")

      k, v = line.split("=", 2) # BUG: keys containing '=' do not round-trip
      v = v[2..].split[1].upcase if v.start_with?("#!") # BUG: NoMethodError on "#!x"
      st.put(k, v)
    end
    st
  end
end
