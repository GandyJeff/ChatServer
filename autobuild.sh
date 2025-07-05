########################################################################
# File Name:autobuild.sh
# Author:JoveStone
# mail:94220388@qq.com
# Created Time:2025-07-05 18:47:20
#########################################################################
#!/bin/bash

set -x

rm -rf 'pwd'/build/*
cd ./build &&
	cmake .. &&
	make


