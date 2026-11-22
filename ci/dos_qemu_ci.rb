# frozen_string_literal: true

# @file ci/dos_qemu_ci.rb
# @brief Headless QEMU MS-DOS (1.25, 2.00, 4.00) & FreeDOS 1.3 verifier for win-cmd-asm.
# @description
#   Inspired by SamuelMarks/c-ci: builds or reuses Bento DOS disk images
#   (via SamuelMarks/bento), boots an ephemeral copy-on-write QCOW2 guest under
#   headless QEMU, uploads WINCMD.COM, WINCMD.MSG, and ASMCONV.COM over COM1
#   (SERGET.COM), and runs live command-processor verification inside real DOS.

require "fileutils"
require "rbconfig"
require "socket"

module WinCmdDosQemuCI
  BS = 92.chr.freeze

  module_function

  def load_bento_plugins!(bento_dir)
    lib_dir = File.join(bento_dir, "lib")
    raise "Bento lib directory not found: #{lib_dir}" unless File.directory?(lib_dir)

    $LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
    require "bento/vagrant_msdos"
    require "bento/vagrant_freedos"
    lib_dir
  end

  def find_free_port(host = "127.0.0.1", preferred = 2324)
    server = TCPServer.new(host, preferred)
    port = server.addr[1]
    server.close
    port
  rescue Errno::EADDRINUSE, Errno::EACCES
    server = TCPServer.new(host, 0)
    port = server.addr[1]
    server.close
    port
  end

  def resolve_qemu_binary
    %w[qemu-system-i386 qemu-system-x86_64].each do |bin|
      ENV["PATH"].to_s.split(File::PATH_SEPARATOR).each do |dir|
        cand = File.join(dir, bin)
        return cand if File.executable?(cand)
        cand_exe = File.join(dir, "#{bin}.exe")
        return cand_exe if File.file?(cand_exe)
      end
    end
    raise "Neither qemu-system-i386 nor qemu-system-x86_64 was found in PATH"
  end

  def resolve_qemu_accel
    return ENV["DOS_QEMU_ACCEL"] if ENV["DOS_QEMU_ACCEL"] && !ENV["DOS_QEMU_ACCEL"].strip.empty?

    host_os = RbConfig::CONFIG["host_os"].to_s.downcase
    host_cpu = RbConfig::CONFIG["host_cpu"].to_s.downcase
    return "tcg" if host_cpu.include?("arm") || host_cpu.include?("aarch64")
    return "hvf:tcg" if host_os.include?("darwin")
    return "kvm:tcg" if host_os.include?("linux") && File.writable?("/dev/kvm")

    "tcg"
  end

  def ensure_bento_image!(bento_dir, os_target)
    builds_dir = File.join(bento_dir, "builds")
    FileUtils.mkdir_p(builds_dir)

    case os_target
    when "freedos"
      qcow = Dir.glob(File.join(builds_dir, "**", "*freedos*.qcow2")).first
      unless qcow
        builder = File.join(bento_dir, "freedos-builder.sh")
        builder = File.join(bento_dir, "msdos-builder.sh") unless File.exist?(builder)
        ok = system("sh", builder, "freedos", chdir: bento_dir)
        raise "Failed building FreeDOS Bento image via #{builder}" unless ok
        qcow = Dir.glob(File.join(builds_dir, "**", "*freedos*.qcow2")).first
      end
      raise "FreeDOS QCOW2 image not found under #{builds_dir}" unless qcow
      [qcow, :freedos, "1.3"]
    when "msdos-4.0", "msdos-2.0", "msdos-1.25"
      ver = os_target.sub("msdos-", "")
      tag = ver.tr(".", "_")
      qcow = Dir.glob(File.join(builds_dir, "**", "*msdos*#{ver}*.qcow2")).first ||
             Dir.glob(File.join(builds_dir, "**", "*msdos*#{tag}*.qcow2")).first
      unless qcow
        builder = File.join(bento_dir, "msdos-builder.sh")
        ok = system("sh", builder, ver, chdir: bento_dir)
        raise "Failed building MS-DOS #{ver} Bento image via #{builder}" unless ok
        qcow = Dir.glob(File.join(builds_dir, "**", "*msdos*#{ver}*.qcow2")).first ||
               Dir.glob(File.join(builds_dir, "**", "*msdos*#{tag}*.qcow2")).first
      end
      raise "MS-DOS #{ver} QCOW2 image not found under #{builds_dir}" unless qcow
      [qcow, :msdos, ver]
    else
      raise "Unsupported DOS QEMU target: #{os_target}"
    end
  end

  def stop_qemu!(pid_file, *extra_files)
    if pid_file && File.exist?(pid_file)
      pid = File.read(pid_file).strip.to_i
      if pid.positive?
        begin
          Process.kill("TERM", pid)
          20.times do
            sleep(0.05)
            Process.kill(0, pid)
          end
          Process.kill("KILL", pid)
        rescue Errno::ESRCH, Errno::EPERM
          nil
        end
      end
      FileUtils.rm_f(pid_file)
    end
    extra_files.compact.each { |f| FileUtils.rm_f(f) }
  end

  def build_communicator(flavor, ver, host, port, timeout_sec)
    if flavor == :msdos
      cfg = Bento::VagrantMsdos::Config.new
      cfg.host = host
      cfg.port = port
      cfg.dos_version = ver
      cfg.boot_timeout = timeout_sec
      cfg.finalize!
      vm = Struct.new(:communicator, :guest).new(:msdos, :msdos)
      conf = Struct.new(:vm, :msdos).new(vm, cfg)
      mach = Struct.new(:config, :data_dir).new(conf, nil)
      Bento::VagrantMsdos::Communicator.new(mach)
    else
      cfg = Bento::VagrantFreedos::Config.new
      cfg.host = host
      cfg.port = port
      cfg.boot_timeout = timeout_sec
      cfg.finalize!
      vm = Struct.new(:communicator, :guest).new(:freedos, :freedos)
      conf = Struct.new(:vm, :freedos).new(vm, cfg)
      mach = Struct.new(:config, :data_dir).new(conf, nil)
      Bento::VagrantFreedos::Communicator.new(mach)
    end
  end

  def verify_target!(os_target:, bento_dir:, build_dir:, toolchain: "nasm")
    load_bento_plugins!(bento_dir)

    dos_dir = File.join(build_dir, toolchain, "dos16")
    wincmd = File.join(dos_dir, "WINCMD.COM")
    wincmd1 = File.join(dos_dir, "WINCMD1.COM")
    winmsg = File.join(dos_dir, "WINCMD.MSG")
    asmconv = File.join(dos_dir, "ASMCONV.COM")
    mem_com = File.join(dos_dir, "MEM.COM")
    hostname_com = File.join(dos_dir, "HOSTNAME.COM")
    whoami_com = File.join(dos_dir, "WHOAMI.COM")
    [wincmd, wincmd1, winmsg, asmconv, mem_com, hostname_com, whoami_com].each do |bin|
      raise "Missing built DOS16 artifact: #{bin}" unless File.file?(bin)
    end

    base_qcow2, flavor, ver = ensure_bento_image!(bento_dir, os_target)
    work_dir = ENV["DOS_WORK_DIR"] || File.join(Dir.pwd, "build", "qemu-work")
    FileUtils.mkdir_p(work_dir)

    host = "127.0.0.1"
    port = find_free_port(host, 2324)
    overlay = File.join(work_dir, "#{os_target}_#{port}_c.qcow2")
    pid_file = File.join(work_dir, "#{os_target}_#{port}.pid")
    FileUtils.rm_f([overlay, pid_file])

    qemu_img = ENV["PATH"].to_s.split(File::PATH_SEPARATOR).map { |d| File.join(d, "qemu-img") }.find { |p| File.executable?(p) }
    if qemu_img
      ok = system(qemu_img, "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", base_qcow2, overlay)
      raise "qemu-img create failed for #{overlay}" unless ok
    else
      FileUtils.cp(base_qcow2, overlay)
    end

    qemu_bin = resolve_qemu_binary
    accel = resolve_qemu_accel
    qemu_args = [
      qemu_bin,
      "-machine", "pc,accel=#{accel}",
      "-m", "64M",
      "-smp", "1",
      "-rtc", "base=localtime",
      "-drive", "file=#{overlay},format=qcow2,if=ide,index=0,media=disk",
      "-serial", "telnet:#{host}:#{port},server,nowait",
      "-display", "none",
      "-daemonize",
      "-pidfile", pid_file
    ]
    puts "[dos-qemu] Booting #{os_target} (accel=#{accel}, serial=#{host}:#{port})..."
    raise "Failed to launch QEMU for #{os_target}" unless system(*qemu_args)

    begin
      comm = build_communicator(flavor, ver, host, port, 120)
      raise "Timed out waiting for #{os_target} serial prompt" unless comm.wait_for_ready(120)

      upload_prefix = os_target == "msdos-1.25" ? "A:" : "C:#{BS}"
      cmd_prefix = %w[msdos-1.25 msdos-2.0].include?(os_target) ? "" : "C:#{BS}"
      comm.fast_upload_file!(wincmd, "#{upload_prefix}WINCMD.COM")
      comm.fast_upload_file!(wincmd1, "#{upload_prefix}WINCMD1.COM")
      comm.fast_upload_file!(winmsg, "#{upload_prefix}WINCMD.MSG")
      comm.fast_upload_file!(asmconv, "#{upload_prefix}ASMCONV.COM")
      comm.fast_upload_file!(mem_com, "#{upload_prefix}MEM.COM")
      comm.fast_upload_file!(hostname_com, "#{upload_prefix}HOSTNAME.COM")
      comm.fast_upload_file!(whoami_com, "#{upload_prefix}WHOAMI.COM")

      checks = [
        ["#{cmd_prefix}WINCMD.COM /C VER", "WINCMD"],
        ["#{cmd_prefix}WINCMD1.COM /C VER", "WINCMD1"],
        ["#{cmd_prefix}WINCMD.COM /C ECHO QEMU_OK_#{os_target.upcase}", "QEMU_OK_"],
        ["#{cmd_prefix}WINCMD.COM /C SET /A X=6*7", "42"],
        ["#{cmd_prefix}WINCMD.COM /C IF EXIST NUL ECHO NUL_QEMU_OK", "NUL_QEMU_OK"],
        ["#{cmd_prefix}WINCMD.COM /C BREAK", "BREAK IS"],
        ["#{cmd_prefix}WINCMD.COM /C DATE /T", "-"],
        ["#{cmd_prefix}MEM.COM", "CONVENTIONAL"],
        ["#{cmd_prefix}HOSTNAME.COM", "DOSPC"],
        ["#{cmd_prefix}WHOAMI.COM /USER", "S-1-5-21"]
      ]
      checks << ["#{cmd_prefix}ASMCONV.COM /?", "ASMCONV"] unless os_target == "msdos-1.25"

      checks.each do |cmd, expected|
        out = +""
        rc = comm.execute(cmd, error_check: false) { |_t, data| out << data }
        puts "[dos-qemu][#{os_target}] #{cmd} -> rc=#{rc}"
        puts out unless out.strip.empty?
        if expected && !out.upcase.include?(expected.upcase)
          raise "Expected '#{expected}' in output of '#{cmd}' on #{os_target}, got: #{out.inspect}"
        end
      end
      puts "[dos-qemu][#{os_target}] PASS"
    ensure
      stop_qemu!(pid_file, overlay)
    end
  end
end

if __FILE__ == $PROGRAM_NAME
  os_target = ENV.fetch("WINCMD_DOS_OS", "msdos-4.0")
  bento_dir = ENV.fetch("BENTO_DIR")
  build_dir = ENV.fetch("BUILD_DIR", File.join(Dir.pwd, "build"))
  toolchain = ENV.fetch("TOOLCHAIN", "nasm")
  WinCmdDosQemuCI.verify_target!(
    os_target: os_target,
    bento_dir: bento_dir,
    build_dir: build_dir,
    toolchain: toolchain
  )
end
