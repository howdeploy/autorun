# Loaded before LonaRPG's external scripts. Ruby 1.9/RGSS3 compatible.
module AutorunDiagnostics
  def self.record(message)
    p "[RUBY] #{message}"
    STDOUT.flush
  rescue StandardError
    nil
  end

  def self.start(path)
    started = Time.now
    record("BEGIN path=#{path}")
    started
  end

  def self.finish(path, started)
    record("END path=#{path} elapsed_ms=#{((Time.now - started) * 1000).to_i}")
  rescue StandardError
    nil
  end

  def self.fail(path, started, error)
    elapsed = started ? ((Time.now - started) * 1000).to_i : -1
    record("FAIL path=#{path} elapsed_ms=#{elapsed} class=#{error.class} message=#{error.message.to_s[0, 320]}")
    (error.backtrace || []).first(32).each { |frame| record("TRACE #{frame[0, 384]}") }
  rescue StandardError
    nil
  end
end
