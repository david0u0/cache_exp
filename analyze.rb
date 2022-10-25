#! /usr/bin/env ruby

def stastistic(cost)
  cnt = cost.length
  avg = cost.sum(0.0) / cnt
  sum = cost.sum(0.0) { |i| (i - avg) ** 2 }
  var = sum / cnt
  std_dev = Math.sqrt(var)

  lower = avg - std_dev * 2
  upper = avg + std_dev * 2

  normal_sum = 0
  normal_cnt = 0
  cost.each do |c|
    if c > lower && c < upper
      normal_cnt += 1
      normal_sum += c
    end
  end
  normal_avg = 1.0 * normal_sum / normal_cnt

  [cnt, normal_cnt, avg, normal_avg, std_dev]
end

class Line
  def initialize
    @cost = []
  end
  def process(s)
    s = s.chop
    if s.include?("cost=")
      m, c = s.split("cost=")
      @cost.push(c.to_i)
      @msg = m
    else
      @cost = []
      @msg = s
    end
  end
  def get_msg
    if @cost.length == 0
      @msg
    else
      cnt, normal_cnt, avg, normal_avg, std_dev = stastistic(@cost)
      "#{@msg}normal avg cost = %.2f (count = %d, normal count = %d, avg = %.2f, std dev = %.2f)" % [normal_avg, cnt, normal_cnt, avg, std_dev]
    end
  end
end

lines1 = []
lines2 = []
`ls *.log`.split do |log|
  processing_2 = false
  i = 0
  `cat  #{log}`.lines do |l|
    if !processing_2 && l.include?("diff")
      processing_2 = true
      i = 0
    end

    lines = if processing_2
             lines2
           else
             lines1
           end

    if lines.length <= i
      lines.push(Line.new)
    end
    lines[i].process(l)
    i += 1
  end
end

File.open("result_same", 'w') do |file|
  lines1.each do |l|
    file.puts l.get_msg
  end
end

File.open("result_diff", 'w') do |file|
  lines2.each do |l|
    file.puts l.get_msg
  end
end
