#! /usr/bin/ruby -w
require "timeout"
require "socket"
require 'net/http'
require 'uri'

$servers = IO.readlines('aprs2-servers').map{|s| s.chomp}
unless $servers && $servers.size > 0
  puts "Server file 'aprs2-servers' missing"
  exit 1
end

def main
  unless ARGV.length == 2 && ARGV[1] =~ /^\d+\z/
    puts "Usage: #{$0} hostname port"
    exit 1
  end

  host = ARGV[0]
  port = ARGV[1]

  puts "%%% Connecting to #{host}:#{port}"
  sock = TCPSocket.new host, port
  while line = sock.gets # Read lines from socket
    line.chomp!
    line.gsub!("\r", '')
    puts "#{line}"
    if line =~ /^APTW01,TCPIP.*X1w/
      send_cwop(line)
    end
  end
end

def send_cwop(line)
  usr = "user N6TVE-11 pass 11394 vers beaglebone-1w 1.00\r\n"
  msg = "N6TVE-11>#{line}\r\n"
  #puts "Message:\n#{usr}#{msg}"
  fd = File.new('/var/log/cwop.log', "a")
  fd.puts(Time.now.strftime("%y/%m/%d %H:%M:%S ") + msg)
  fd.close

  #servers = ['iad2.aprs2.net'] unless servers && servers.size > 0
  $servers.each do |s|
    begin
      http = Net::HTTP.start(s, 8080)
      http.open_timeout = 5
      http.read_timeout = 30
      res = http.request_post('/', usr+msg,
                              'Content-Type' => 'application/octet-stream',
                              'Accept-Type' => 'text/plain')
      puts "Response from #{s}: #{res.code} #{res.message}"
      puts res.body if res.code == '200'
      return if res.code == '200'
    rescue StandardError => e
      puts "Failed to post to CWOP #{s}: #{e}"
    end
  end
end

main
__END__


#cr=$'\r'
#IFS=''
#data="$*"
#data="${data/h100/h00}"
##echo "Data=$* $data"
##nc cwop.aprs.net 14580 <<"EOF"
#od -c <<EOF
#user N6TVE-11 pass -1 vers linux-1wire 2.00$cr
#N6TVE-11>APRS,TCPXX*:/!3429.97N/06010.91W${data}eThermd21w$cr
#EOF

