#!/usr/bin/env ruby
# Guards the bundled configuration against the trap where an example is written
# as "# key: value": deleting the '#' then leaves one space too many and the
# file no longer parses, which crash-loops MediaMTX. Every commented example
# must therefore place its '#' in an indentation column, so that removing that
# single character yields correctly indented YAML.
#
# Psych.parse is used rather than YAML.load: this is a syntax check, and values
# such as ":8554" would otherwise be deserialised as Ruby symbols.

require 'yaml'

FILE = File.expand_path('../app/mediamtx.defaults.yml', __dir__)
lines = File.readlines(FILE).map(&:chomp)

# Settings are lowerCamelCase, so a comment that hides one is distinguishable
# from prose ("# Notes:", "# Available values are: ...").
SETTING = /\A\s*(-\s+)?[a-z][A-Za-z0-9_]*:(\s|\z)/.freeze

def example?(line)
  return false unless line =~ /\A\s*#/

  uncommented = line.sub('#', '')
  return false if uncommented =~ /\A\s*#/ # nested comment, stays commented

  uncommented =~ SETTING
end

def separator?(line)
  line !~ /\A\s*#/ || line =~ /\A\s*#\s*\z/
end

blocks = []
current = []
lines.each_with_index do |line, i|
  if example?(line)
    current << i
  elsif separator?(line)
    blocks << current unless current.empty?
    current = []
  end
end
blocks << current unless current.empty?

failures = 0

begin
  YAML.parse(lines.join("\n"))
  puts 'ok    shipped defaults parse'
rescue Psych::SyntaxError => e
  failures += 1
  puts "FAIL  shipped defaults do not parse: #{e.message}"
end

blocks.each do |block|
  text = lines.dup
  block.each { |i| text[i] = lines[i].sub('#', '') }

  # A few settings reject an empty value, so they ship as "key: []" and the
  # example replaces that line instead of being uncommented in place.
  first = text[block.first]
  if first =~ /\A(\s*)([a-z][A-Za-z0-9_]*):\s*\z/
    placeholder = "#{Regexp.last_match(1)}#{Regexp.last_match(2)}: []"
    text = text.reject { |line| line == placeholder }
  end

  label = "line #{block.first + 1}: #{lines[block.first].strip[0, 58]}"
  begin
    YAML.parse(text.join("\n"))
    puts "ok    uncommented #{label}"
  rescue Psych::SyntaxError => e
    failures += 1
    puts "FAIL  uncommented #{label}"
    puts "      #{e.message}"
  end
end

abort "\n#{failures} example(s) break the configuration when uncommented" if failures.positive?

puts "\n#{blocks.size} example block(s) verified"
