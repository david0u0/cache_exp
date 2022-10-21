set -exu

DFLAG=""
if [ $1 == "baseline" ]; then
    DFLAG="-D BASELINE"
fi

OUT=output/$1
echo output to $OUT

g++ main.cpp -O3 $DFLAG -std=c++17 -pthread

mkdir -p output
mkdir -p $OUT
for i in {1..50}
do
    echo running $i
    ./a.out > $OUT/$i.log
done

cd $OUT
../../analyze.rb > result
