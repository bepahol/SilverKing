package com.ms.silverking.cloud.dht.net.protocol;

import com.ms.silverking.cloud.dht.ValueCreator;
import com.ms.silverking.cloud.dht.net.MessageGroupGlobals;
import com.ms.silverking.numeric.NumConversion;

public abstract class MessageFormat {
    public static final int   preambleSize = MessageGroupGlobals.preamble.length;
    public static final int   protocolVersionSize = 1;
    public static final int   lengthSize = NumConversion.BYTES_PER_INT;
    public static final int   typeSize = 1;
    public static final int   uuidMSLSize = NumConversion.BYTES_PER_LONG;
    public static final int   uuidLSLSize = NumConversion.BYTES_PER_LONG;
    public static final int   contextSize = NumConversion.BYTES_PER_LONG;
    public static final int   originatorSize = ValueCreator.BYTES;
    public static final int   deadlineRelativeMillisSize = NumConversion.BYTES_PER_INT;
    public static final int   forwardSize = 1;
    
    public static final int   preambleOffset = 0;
    public static final int   protocolVersionOffset = preambleOffset + preambleSize;
    public static final int   lengthOffset = protocolVersionOffset + protocolVersionSize;
    public static final int   typeOffset = lengthOffset + lengthSize;
    public static final int   uuidMSLOffset = typeOffset + typeSize;
    public static final int   uuidLSLOffset = uuidMSLOffset + uuidMSLSize;
    public static final int   contextOffset = uuidLSLOffset + uuidLSLSize;
    public static final int   originatorOffset = contextOffset + contextSize;
    public static final int   deadlineRelativeMillisOffset = originatorOffset + originatorSize;
    public static final int   forwardOffset = deadlineRelativeMillisOffset + deadlineRelativeMillisSize;
    public static final int   leadingBufferEndOffset = forwardOffset + forwardSize;
    // end is exclusive of the buffer
    
    public static final int   leadingBufferSize = leadingBufferEndOffset;
}
