package com.ms.silverking.cloud.dht.net.protocol;

import java.nio.ByteBuffer;

import com.ms.silverking.cloud.dht.ValueCreator;
import com.ms.silverking.cloud.dht.client.ChecksumType;
import com.ms.silverking.cloud.dht.common.CCSSUtil;
import com.ms.silverking.numeric.NumConversion;

public class PutBaseMessageFormat extends KeyValueMessageFormat {    
    // options buffer    
    public static final int    versionSize = NumConversion.BYTES_PER_LONG;
    public static final int    ccssSize = 2;
    public static final int    valueCreatorSize = ValueCreator.BYTES;
    
    public static final int    versionOffset = 0;
    public static final int    ccssOffset = versionOffset + versionSize;
    public static final int    valueCreatorOffset = ccssOffset + ccssSize;
    
    public static ChecksumType getChecksumType(ByteBuffer optionsByteBuffer) {
        return CCSSUtil.getChecksumType(optionsByteBuffer.getShort(ccssOffset));
    }
    
    public static byte getStorageState(ByteBuffer optionsByteBuffer) {
        return CCSSUtil.getStorageState(optionsByteBuffer.getShort(ccssOffset));
    }
}
