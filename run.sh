set -exu

DFLAG=""
if [ $1 == "baseline" ]; then
    DFLAG="-D BASELINE"
fi

OUT=output/$1
echo output to $OUT

g++ main.cpp -O3 $DFLAG -std=c++17 -pthread

mkdir -p output
rm $OUT -rf
mkdir -p $OUT
for i in {1..200}
do
    echo running $i
    LOG=$OUT/$i.log
    ./a.out "rw same" >> $LOG
    ./a.out "sharded same" >> $LOG
    ./a.out "l2 same" >> $LOG
    ./a.out "l2 sharded same" >> $LOG
    ./a.out "l2 local same" >> $LOG
    ./a.out "single same" >> $LOG

    ./a.out "rw diff" >> $LOG
    ./a.out "sharded diff" >> $LOG
    ./a.out "l2 diff" >> $LOG
    ./a.out "l2 sharded diff" >> $LOG
    ./a.out "l2 local diff" >> $LOG
    ./a.out "single diff" >> $LOG
done

cd $OUT
../../analyze.rb
