#! /bin/sh
echo "testing conversion of pressure to height"
../src/binSrc/fimex --input.file=verticalPressure.nc --verticalInterpolate.type=height --verticalInterpolate.method=linear --verticalInterpolate.level1=0,50,100,250,500,750,1000,5500,10000,20000 --output.file=out.nc
if [ $? != 0 ]; then
  echo "failed converting pressure to height"
  rm -f out.nc
  exit 1
fi
diff verticalPressureHeight.nc out.nc 
if [ $? != 0 ]; then
  echo "failed diff pressure to height"
  rm -f out.nc
  exit 1
fi
rm -f out.nc
echo "success"
exit 0
