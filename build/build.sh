#!/bin/ksh

source lib/common.lib

f_checkAndSetBuildTimestamp "$1"

output_filename=$(f_getBuild_RunOutputFilename)
{
	date
	echo
	echo "Build Flow"
	echo "1. Build Silverking"
	echo "2. Build Silverking client"
	echo "3. Build Silverking fs"
	echo
	
	f_startGlobalTimer

	f_printStep "1" "Build Silverking"
	./$BUILD_SILVERKING_SCRIPT_NAME
	f_startSilverking
	./$TEST_SILVERKING_SCRIPT_NAME

	CC=$GPP_RHEL6 
	f_printStep "2" "Build Silverking client"
	GCC_R_LIB=$GCC_RHEL6_LIB
	   SK_VER=""
	 CC_FLAGS="-g -O2"
	./$BUILD_SILVERKING_CLIENT_SCRIPT_NAME "$CC" "$GCC_R_LIB" "$SK_VER" "$CC_FLAGS"
	./$TEST_SILVERKING_CLIENT_SCRIPT_NAME
	
	f_printStep "3" "Build Silverking FS"
	FUSE_INC_DIR=$FUSE_RHEL6_INC_DIR
	FUSE_LIB_DIR=$FUSE_RHEL6_LIB_DIR
	./$BUILD_SILVERKING_FS_SCRIPT_NAME "$CC" "$FUSE_INC_DIR" "$FUSE_LIB_DIR"
	f_startSkfs
	./$TEST_SILVERKING_FS_SCRIPT_NAME

	f_stopSkfs
	f_stopSilverking
	
	f_printSummary_BuildFlow

	f_printGlobalElapsed
	f_printFileOutputLine "$output_filename"
} 2>&1 | tee $output_filename
