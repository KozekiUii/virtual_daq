#!
for i in $(seq 1 50); do
    echo "round $i"
    sudo insmod vdaq.ko || exit 1
    sudo chmod 666 /dev/vdaq0 || exit 1
    ../../app/reader || exit 1
    sudo rmmod vdaq || exit 1
done