#!/bin/sh

echo $# $1
if [ "$#" != "1" ]; then
	exit 0;
fi

echo "deploying..."
cp utils/libcube_utils.so $1/src/swift/cube/lib
cp client/libcube_client.so $1/src/swift/cube/lib
cp client/libstat_tips.so $1/src/swift/cube/lib
cp utils/*.h $1/src/swift/cube/include
cp client/cube_client.h $1/src/swift/cube/include
cp client/stat_tips.h $1/src/swift/cube/include

if [ -d "$1/out/for_deploy" ]; then
	echo "deploy cube"
	cp out/usr/bin/* $1/out/for_deploy/usr/bin
	cp out/usr/lib/* $1/out/for_deploy/usr/lib
fi

