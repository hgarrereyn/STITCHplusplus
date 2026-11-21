# JSON, XML (XmlMini with REXML), message verifier / encryptor, Gzip, SecureCompare, KeyGenerator,
# caches, OrderedOptions, HashWithIndifferentAccess, CurrentAttributes-free utilities.
register :json_encode_decode, { x: Borrow[Val] } do |x|
  text = ActiveSupport::JSON.encode(x)
  back = ActiveSupport::JSON.decode(text)
  Stitch.log("#{text[0, 300]}")
  Stitch.check(ActiveSupport::JSON.encode(back) == text || text.include?("NaN") || text.match?(/Infinity/), "encode(decode(encode(x))) == encode(x)")
end
register :json_decode, { text: Str[256] }, New[Val] | nil do |text|
  v = ActiveSupport::JSON.decode(text)
  v.is_a?(Hash) || v.is_a?(Array) ? v : nil
end
require "rexml/document"
XmlText = Stitch.datatype("XmlText")
Stitch.decoder(XmlText) do |s|
  inner = +""
  while s.more
    tag = s.pick(%w[a b item items user-name x])
    type = s.pick(["", ' type="integer"', ' type="boolean"', ' type="date"', ' type="datetime"', ' type="array"', ' type="yaml"', ' type="symbol"', ' type="decimal"', ' type="base64Binary"', ' nil="true"', ' type="file"'])
    inner << "<#{tag}#{type}>#{s.coin ? s.str(8) : s.pick(['1', 'true', '2020-01-01', '1.5', '<x>1</x>', '', '---\n- 1', 'YQ=='])}</#{tag}>"
  end
  "<hash>#{inner}</hash>"
end
register :hash_from_xml, { text: Str[512, XmlText], unsafe: Bool }, New[Val] | nil, bails: ARG + [REXML::ParseException, ActiveSupport::XMLConverter::DisallowedType] do |text, unsafe|
  Hash.from_xml(text, unsafe ? %w[yaml symbol] : [])
end
register :xml_roundtrip, { h: Borrow[Val] } do |h|
  Stitch.require(h.is_a?(Hash) && !h.empty? && h.keys.all? { |k| k.is_a?(String) && k.match?(/\A[a-z]+\z/) }, "needs a hash with simple string keys")
  Stitch.require(h.values.all? { |v| v.is_a?(String) || v.is_a?(Integer) || v.nil? || v == true || v == false }, "needs scalar values")
  xml = h.to_xml(root: "r")
  back = Hash.from_xml(xml)["r"] || {}
  Stitch.log(xml)
  Stitch.check(back.transform_values { |v| v == "" ? nil : v } == h.transform_values { |v| v == "" ? nil : v }, "Hash.from_xml(h.to_xml) == h")
end

def marshalable(x)
  Marshal.dump(x)
rescue TypeError
  Stitch.bail("not marshalable")
end

register :message_verifier, { x: Borrow[Val], secret: Str[16], digest: OneOf["SHA1", "SHA256", "SHA512"], s: Src[8] } do |x, secret, digest, s|
  marshalable(x)
  v = ActiveSupport::MessageVerifier.new(secret, digest: digest, serializer: s.coin ? JSON : Marshal)
  token = v.generate(x, purpose: s.pick([nil, :login]))
  back = v.verify(token, purpose: v == v ? (s.coin ? nil : nil) : nil) rescue (v.verified(token, purpose: :login))
  Stitch.check(v.valid_message?(token), "a generated message is valid")
  Stitch.check(v.verified(token + "x").nil?, "a tampered message does not verify")
  back
end
register :message_encryptor, { x: Borrow[Val], s: Src[8] } do |x, s|
  marshalable(x)
  key = ActiveSupport::KeyGenerator.new("secret", iterations: 2).generate_key("salt", s.pick([32, 16, 32]))
  cipher = key.bytesize == 16 ? "aes-128-gcm" : s.pick(["aes-256-gcm", "aes-256-cbc"])
  e = ActiveSupport::MessageEncryptor.new(key, cipher: cipher)
  data = e.encrypt_and_sign(x)
  Stitch.check(Marshal.dump(e.decrypt_and_verify(data)) == Marshal.dump(x) || x.respond_to?(:nan?), "decrypt_and_verify(encrypt_and_sign(x)) == x")
end
register :gzip_roundtrip, { data: Bytes[256] } do |data|
  Stitch.check(ActiveSupport::Gzip.decompress(ActiveSupport::Gzip.compress(data)).b == data.b, "Gzip round trip")
end
register :secure_compare, { a: Str[16], b: Str[16] } do |a, b|
  Stitch.check(ActiveSupport::SecurityUtils.secure_compare(a, b) == (a == b), "secure_compare == ==")
end

CACHE_OPS = %i[write read fetch delete exist? increment decrement write_multi read_multi fetch_multi delete_matched cleanup clear]
register :cache_ops, { s: Src[64], kind: OneOf[:memory, :null] } do |s, kind|
  c = kind == :memory ? ActiveSupport::Cache::MemoryStore.new(size: 2048) : ActiveSupport::Cache::NullStore.new
  model = {}
  8.times do
    break unless s.more
    k = s.pick(%w[a b c d/e])
    case s.pick(CACHE_OPS)
    when :write then v = s.range(0, 9); c.write(k, v); model[k] = v
    when :read then r = c.read(k); Stitch.check(kind == :null || r == model[k] || model[k].nil?, "read returns what was written")
    when :fetch then r = c.fetch(k) { 42 }; model[k] ||= 42 if kind == :memory; Stitch.check(kind == :null || r == model[k], "fetch")
    when :delete then c.delete(k); model.delete(k)
    when :increment then c.increment(k); model[k] = c.read(k) if kind == :memory
    when :decrement then c.decrement(k); model[k] = c.read(k) if kind == :memory
    when :write_multi then c.write_multi(k => 1, "z" => 2); model[k] = 1; model["z"] = 2
    when :read_multi then r = c.read_multi(k, "z"); Stitch.check(kind == :null || r.fetch(k, nil) == model[k], "read_multi returns what was written")
    when :fetch_multi then c.fetch_multi(k, "y") { |x| x.size }; [k, "y"].each { |kk| model[kk] = c.read(kk) } if kind == :memory
    when :delete_matched then c.delete_matched(/a|b/); model.delete_if { |kk, _| kk.match?(/a|b/) }
    else c.cleanup
    end
    model.delete(k) if %i[increment decrement fetch_multi].include?(:x)
  end
end

register :indifferent_access, { h: Borrow[Val], k: OneOf["a", :a, "b", :b, 1, "1"] } do |h, k|
  Stitch.require(h.is_a?(Hash), "not a hash")
  w = h.with_indifferent_access
  Stitch.check(w[k.to_s] == w[k.is_a?(Integer) ? k : k.to_sym] || k.is_a?(Integer), "string and symbol keys are the same")
  w2 = w.merge(k => 1)
  Stitch.check(w2[k.to_s] == 1 || k.is_a?(Integer), "merge with a symbol key is visible as a string")
  w.deep_merge(h).to_hash
  w.slice(k).keys.all?(String) || Stitch.check(false, "slice keeps string keys")
end

register :ordered_options, { k: Str[12, Word], v: Borrow[Val] } do |k, v|
  Stitch.require(k.match?(/\A[a-z_]+\z/) && !ActiveSupport::OrderedOptions.method_defined?(k), "not a plain name")
  o = ActiveSupport::OrderedOptions.new
  o.public_send("#{k}=", v)
  Stitch.check(o.public_send(k).equal?(v) && o[k.to_sym].equal?(v), "OrderedOptions reads back")
end

register :string_to_time_oracle, { y: Int[1900, 2100], mo: Int[1, 12], d: Int[1, 28], h: Int[0, 23], mi: Int[0, 59], sec: Int[0, 59] } do |y, mo, d, h, mi, sec|
  t = Time.utc(y, mo, d, h, mi, sec)
  Stitch.check(t.to_fs(:db).to_time(:utc) == t, "to_fs(:db).to_time round trip")
  Stitch.check(t.iso8601.to_datetime.to_time.utc == t, "iso8601.to_datetime round trip")
  Stitch.check(t.to_date.to_fs(:db).to_date == t.to_date, "Date to_fs(:db) round trip")
end

register :module_delegation, { n: Str[12, Word] } do |n|
  Stitch.require(n.match?(/\A[a-z_]+\z/), "not a method name")
  klass = Class.new do
    attr_accessor :target
    class_attribute :setting, default: 1
    mattr_accessor :shared
    delegate :length, :upcase, to: :target, allow_nil: true
  end
  o = klass.new
  o.target = n
  Stitch.check(o.length == n.length && o.upcase == n.upcase, "delegate")
  o.target = nil
  Stitch.check(o.length.nil?, "allow_nil delegation returns nil")
  klass.setting = n
  Stitch.check(klass.new.setting == n, "class_attribute")
end
