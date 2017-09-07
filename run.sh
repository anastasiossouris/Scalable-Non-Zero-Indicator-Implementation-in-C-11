make -f makefile-no-contention clean
make -f makefile-no-contention
make -f makefile-semi-contention clean
make -f makefile-semi-contention
make -f makefile-full-contention clean
make -f makefile-full-contention

echo "Running no-contention..."
echo ""
./snzi_no

echo "Running semi-contention..."
echo ""
./snzi_semi

echo "Running full-contention..."
echo ""
./snzi_full
