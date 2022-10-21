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

  [cnt, normal_cnt, normal_avg, std_dev]
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
      cnt, normal_cnt, avg, std_dev = stastistic(@cost)
      "#{@msg}cost = #{avg} (count = #{cnt}, normal count = #{normal_cnt}, std dev = #{std_dev})"
    end
  end
end

lines = []
`ls *.log`.split do |log|
  i = 0
  `cat #{log}`.lines do |l|
    if lines.length <= i
      lines.push(Line.new)
    end
    lines[i].process(l)
    i += 1
  end
end

lines.each do |l|
  puts l.get_msg
end
