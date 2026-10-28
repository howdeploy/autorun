# Repack known script data without evaluating the game or its scripts.
require 'digest'
require 'fileutils'
require 'json'
require 'zlib'

game, output = ARGV
abort 'Usage: ruby prepare.rb GAME_DIR OUTPUT_DIR' unless game && output && !File.exist?(output)
original = File.binread(File.join(game, 'Data/Scripts.rvdata2'))
expected = '08710c285c50c4fc741c571cefa45270a422f4346fd1bf951d29633c2a57ba60'
abort 'Unknown Scripts.rvdata2; inspect the new game version first' unless Digest::SHA256.hexdigest(original) == expected
entries = Marshal.load(original)
abort 'Unexpected bootstrap structure' unless entries.size == 1 && entries[0][1] == 'LoadUp'
code = Zlib::Inflate.inflate(entries[0][2])
replacements = {
  'p "LoadUp F11 #{file}"' => 'autorun_started = AutorunDiagnostics.start(file)',
  'p "LoadUp F11 #{file} end"' => 'AutorunDiagnostics.finish(file, autorun_started)',
  "rescue => ex\r\n" => "rescue => ex\r\n\t\tAutorunDiagnostics.fail(file, autorun_started, ex)\r\n"
}
replacements.each do |before, after|
  abort "Bootstrap anchor mismatch: #{before}" unless code.scan(before).size == 1
  code = code.sub(before, after)
end
helper = File.binread(File.join(__dir__, 'diagnostics.rb'))
entries[0][2] = Zlib::Deflate.deflate(helper + "\n" + code)
packed = Marshal.dump(entries)
decoded = Marshal.load(packed)
abort 'Repacked bootstrap mismatch' unless decoded[0][0, 2] == entries[0][0, 2] &&
  Zlib::Inflate.inflate(decoded[0][2]) == helper + "\n" + code
importer = File.binread(File.join(game, 'Data/Scripts/102_ScriptImport.rb'))
abort 'Unknown diagnostic importer; review its changes first' unless Digest::SHA256.hexdigest(importer) ==
  '0d84863a3be83debd614f5ef5651779b78bcf450977e82baf7751f54662be2b0'
FileUtils.mkdir_p(File.join(output, 'Data/Scripts'))
File.binwrite(File.join(output, 'original-Scripts.rvdata2'), original)
File.binwrite(File.join(output, 'LoadUp.rb'), helper + "\n" + code)
files = { 'Data/Scripts.rvdata2' => packed, 'Data/Scripts/102_ScriptImport.rb' => importer }
manifest = files.map do |path, bytes|
  File.binwrite(File.join(output, path), bytes)
  { path: path, bytes: bytes.bytesize, sha256: Digest::SHA256.hexdigest(bytes) }
end
File.write(File.join(output, 'manifest.json'), JSON.pretty_generate({
  original_bootstrap_sha256: expected, files: manifest, game_executed: false
}) + "\n")
puts JSON.pretty_generate(manifest)
