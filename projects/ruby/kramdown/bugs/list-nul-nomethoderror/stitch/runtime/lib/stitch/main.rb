# frozen_string_literal: true

# Entry points: the engine's worker, and the reproducer CLI.
#
#   stitch-rb [--trace] [--plan FILE] [--manifest] FILE.stp...                  run programs (reproducer)
#   stitch-rb --each [--timeout S] [--coverage DIR] [--files-from LIST] FILE...  run all, keep going (stitch cov)
#   stitch-rb worker                                                          engine worker (fds 3 and 4)
#
# The harness is every .rb file under <project>/harness (loaded in sorted order).
require "json"

module Stitch
  module Main
    module_function

    def limit_memory
      mb = Integer(ENV.fetch("STITCH_MEMORY_MB", "4096"))
      Process.setrlimit(Process::RLIMIT_AS, mb << 20) if mb.positive?
    rescue StandardError
      nil
    end

    def load_harness(project, instrument: true)
      project = File.expand_path(project)
      ENV["STITCH_PROJECT"] ||= project
      Instrument.install if instrument
      $LOAD_PATH.unshift(project)
      hdir = File.join(project, "harness")
      files = Dir.glob(File.join(hdir, "**", "*.rb")).sort
      abort("[stitch] error: no harness files (#{hdir}/**/*.rb)") if files.empty?
      files.each { |f| Kernel.require(f) }
      abort("[stitch] error: the harness defines no stubs (Stitch.endpoint)") if REG.endpoints.empty?
      REG.inits.each(&:call)
    end

    def run_program(data, trace: false, cmplog: false)
      Instrument.cmplog = cmplog
      RT.run(data, trace: trace)
    end

    # ---------------------------------------------------------------- worker
    def read_exact(io, n)
      b = +"".b
      while b.bytesize < n
        c = io.read(n - b.bytesize)
        raise EOFError if c.nil? || c.empty?

        b << c
      end
      b
    end

    def worker(project)
      rx = IO.new(3, "rb", autoclose: false)
      tx = IO.new(4, "wb", autoclose: false)
      tx.sync = true
      send = ->(tag, payload = "".b) { tx.write(tag.b + [payload.bytesize].pack("L<") + payload.b) }
      limit_memory
      load_harness(project, instrument: ENV["STITCH_FUZZING"] == "1")
      send.call("H", [Instrument::MAP_SIZE].pack("L<") + REG.manifest.b)
      dts = REG.dt_list
      loop do
        begin
          h = read_exact(rx, 5)
          payload = read_exact(rx, h.byteslice(1, 4).unpack1("L<"))
        rescue EOFError
          return
        end
        case h[0]
        when "X"
          flags = payload.unpack1("L<")
          r = run_program(payload.byteslice(4..), trace: flags & 1 != 0, cmplog: flags & 2 != 0)
          send.call(r == :finding ? "C" : "O", RT.results_bytes)
        when "D"
          dt, cap = payload.unpack("L<L<")
          ch = payload.byteslice(8..)
          d = dts[dt]
          v = d&.decoder ? RT.run_decoder(d, Source.new(ch, cap: [ch.bytesize, cap].max), cap) : ch.byteslice(0, cap)
          send.call("R", v)
        when "G"
          dt, seed, cap = payload.unpack("L<Q<L<")
          d = dts[dt]
          out = "".b
          if d&.decoder
            src = Source.new(payload.byteslice(16, cap) || "".b, cap: cap, gen: true, seed: seed, dict: REG.dicts[dt])
            RT.run_decoder(d, src)
            out = src.buf.byteslice(0, cap)
          end
          send.call("R", out)
        when "S"
          dt = payload.unpack1("L<")
          d = dts[dt]
          spans = []
          RT.run_decoder(d, Source.new(payload.byteslice(4..), cap: payload.bytesize - 4, spans: spans)) if d&.decoder
          send.call("R", spans.pack("L<*"))
        when "I"
          dt = payload.unpack1("L<")
          d = dts[dt]
          items = []
          if d&.decoder
            src = Source.new(payload.byteslice(4..), cap: payload.bytesize - 4, items: items)
            RT.run_decoder(d, src)
            src.finish_items
          end
          send.call("R", items.map { |x| x & 0xFFFFFFFF }.pack("L<*"))
        when "T"
          dt = payload.unpack1("L<")
          tok = payload.byteslice(4..)
          toks = REG.dicts[dt]
          toks << tok if toks.size < 4096 && !toks.include?(tok)
        when "P"
          RT.plan&.close
          RT.plan = payload.empty? ? nil : File.open(payload, "w")
        when "Q"
          RT.plan&.close
          return
        end
      end
    end

    # ---------------------------------------------------------------- coverage (stitch cov)
    def coverage_start
      require "coverage"
      Coverage.start(lines: true, branches: true, methods: true)
    end

    def coverage_write(dir, project)
      res = Coverage.result
      inc = ENV.fetch("STITCH_RB_INCLUDE", "").split(",").map(&:strip).reject(&:empty?)
      harness = File.join(File.expand_path(project), "harness")
      Instrument.setup
      lines_total = lines_hit = 0
      report = []
      File.open(File.join(dir, "lcov.info"), "w") do |f|
        res.sort.each do |path, data|
          next if path.start_with?(RUNTIME_DIR)
          next unless path.start_with?(harness) || (inc.empty? ? Instrument.wanted?(path) : inc.any? { |p| Instrument.matches?(path, p) })

          f.puts "TN:", "SF:#{path}"
          # methods: {[klass, name, start_line, start_col, end_line, end_col] => calls}; one
          # function per definition site (a method defined in several classes counts once each)
          fns = {}
          (data[:methods] || {}).each do |(klass, name, line, _c, _el, _ec), c|
            kname = klass.is_a?(Module) && klass.name ? klass.name : klass.to_s
            fn = "#{kname}##{name}"
            prev = fns[fn]
            fns[fn] = [line, (prev ? prev[1] : 0) + c]
          end
          fns.each { |fn, (line, _)| f.puts "FN:#{line},#{fn}" }
          fns.each { |fn, (_, c)| f.puts "FNDA:#{c},#{fn}" }
          f.puts "FNF:#{fns.size}", "FNH:#{fns.count { |_, (_, c)| c.positive? }}"
          lh = lf = 0
          (data[:lines] || []).each_with_index do |c, i|
            next if c.nil?

            f.puts "DA:#{i + 1},#{c}"
            lf += 1
            lh += 1 if c.positive?
          end
          brf = brh = 0
          (data[:branches] || {}).each_value do |arms|
            arms.each do |arm, c|
              f.puts "BRDA:#{arm[2]},#{arm[1]},#{brf},#{c}"
              brf += 1
              brh += 1 if c.positive?
            end
          end
          f.puts "LF:#{lf}", "LH:#{lh}", "BRF:#{brf}", "BRH:#{brh}", "end_of_record"
          lines_total += lf
          lines_hit += lh
          report << format("%-70s %6d %6d %6.1f%%", path, lf, lh, lf.zero? ? 0 : 100.0 * lh / lf)
        end
      end
      File.write(File.join(dir, "report.txt"), (["#{'file'.ljust(70)}  lines    hit  cover"] + report +
        [format("%-70s %6d %6d %6.1f%%", "TOTAL", lines_total, lines_hit, lines_total.zero? ? 0 : 100.0 * lines_hit / lines_total)]).join("\n") + "\n")
      system("genhtml", "-q", "--branch-coverage", "-o", File.join(dir, "html"), File.join(dir, "lcov.info"),
             out: File::NULL, err: File::NULL)
    end

    # ---------------------------------------------------------------- reproducer
    def repro(argv)
      trace = argv.include?("--trace")
      each = argv.include?("--each")
      files = []
      plan = nil
      timeout = 10
      covdir = nil
      project = Dir.pwd
      i = 0
      while i < argv.size
        a = argv[i]
        if %w[--plan --timeout --coverage --project --files-from].include?(a)
          v = argv[i + 1]
          case a
          when "--files-from" then files.concat(File.readlines(v, chomp: true).map(&:strip).reject(&:empty?))
          when "--plan" then plan = v
          when "--timeout" then timeout = Integer(v)
          when "--coverage" then covdir = v
          else project = v
          end
          i += 2
          next
        end
        files << a unless a.start_with?("-")
        i += 1
      end
      if argv.include?("--manifest")
        load_harness(project, instrument: false)
        puts REG.manifest
        return 0
      end
      coverage_start if covdir
      limit_memory
      load_harness(project, instrument: false)
      RT.plan = File.open(plan, "w") if plan
      if each
        require "timeout"
        stats = { "programs" => 0, "ok" => 0, "crashed" => 0, "timeouts" => 0, "invalid" => 0 }
        files.each do |f|
          stats["programs"] += 1
          begin
            r = Timeout.timeout(timeout) { run_program(File.binread(f)) }
            stats[{ ok: "ok", finding: "crashed", invalid: "invalid" }[r]] += 1
          rescue Timeout::Error
            stats["timeouts"] += 1
          end
        end
        coverage_write(covdir, project) if covdir
        puts JSON.generate(stats)
        return 0
      end
      files.each do |f|
        $stderr.write("[stitch] running #{f}\n") if trace
        data = begin
          File.binread(f)
        rescue SystemCallError => e
          $stderr.write("[stitch] cannot open #{f}: #{e.message}\n")
          return 2
        end
        r = run_program(data, trace: trace)
        return 1 if r == :finding
        return 2 if r == :invalid
      end
      RT.plan&.close
      0
    end

    def main(argv)
      if argv.first == "worker"
        i = argv.index("--project")
        worker(i ? argv[i + 1] : Dir.pwd)
        return 0
      end
      if argv.empty? || %w[-h --help].include?(argv.first)
        puts File.read(__FILE__).lines.drop(2).take_while { |l| l.start_with?("#") }.map { |l| l.sub(/^# ?/, "") }.join
        return 0
      end
      repro(argv)
    end
  end
end
