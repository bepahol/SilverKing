package com.ms.silverking.cloud.dht.client.impl;

import java.nio.ByteBuffer;

import com.ms.silverking.cloud.dht.ValueCreator;
import com.ms.silverking.cloud.dht.client.ChecksumType;
import com.ms.silverking.cloud.dht.common.EnumValues;
import com.ms.silverking.cloud.dht.common.RawRetrievalResult;
import com.ms.silverking.cloud.dht.common.SimpleValueCreator;
import com.ms.silverking.io.util.BufferUtil;
import com.ms.silverking.numeric.NumConversion;

public class SegmentationUtil {
    static final int   segmentedValueBufferLength = NumConversion.BYTES_PER_LONG 
                                                    + 2 * NumConversion.BYTES_PER_INT
                                                    + 1;
    private static final int    creatorBytesOffset = 0;
    private static final int    storedLengthOffset = creatorBytesOffset + NumConversion.BYTES_PER_LONG;
    private static final int    uncompressedLengthOffset = storedLengthOffset + NumConversion.BYTES_PER_INT;
    private static final int    checksumTypeOffset = uncompressedLengthOffset + NumConversion.BYTES_PER_INT;
    private static final int    checksumOffset = checksumTypeOffset + 1;
    
    // FUTURE - find a better home
    //public static final int   maxValueSegmentSize = 4; 
    //public static final int   maxValueSegmentSize = 1 * 1024 * 1024; // was 1M 
    public static final int   maxValueSegmentSize = 10 * 1024 * 1024; 
    
    static int getNumSegments(int valueSize, int segmentSize) {
        return (valueSize - 1) / segmentSize + 1;
    }
    
    static byte[] getCreatorBytes(ByteBuffer buf) {
        byte[]  creatorBytes;
        int     dataStart;
        
        buf = buf.asReadOnlyBuffer();
        dataStart = buf.position();
        buf.position(dataStart + creatorBytesOffset);
        creatorBytes = new byte[ValueCreator.BYTES];
        buf.get(creatorBytes);
        return creatorBytes;
    }
    
    public static SimpleMetaData getMetaData(RawRetrievalResult rawResult, ByteBuffer buf) {
        SimpleMetaData  metaData;
        
        metaData = new SimpleMetaData(rawResult);
        metaData = metaData.setStoredLength(getStoredLength(buf));
        metaData = metaData.setUncompressedLength(getUncompressedLength(buf));
        metaData = metaData.setValueCreator(new SimpleValueCreator(getValueCreatorBytes(buf)));
        metaData = metaData.setChecksumTypeAndChecksum(getChecksumType(buf), getChecksum(buf));
        //System.out.printf("%s %s %d\n", buf, getChecksumType(buf), getChecksum(buf).length);
        //System.out.printf("%s\n", StringUtil.byteBufferToHexString(buf));
        return metaData;
    }
    
    public static int getStoredLength(ByteBuffer buf) {
        return buf.getInt(buf.position() + storedLengthOffset);
    }
    
    public static int getUncompressedLength(ByteBuffer buf) {
        return buf.getInt(buf.position() + uncompressedLengthOffset);
    }
    
    static byte[] getValueCreatorBytes(ByteBuffer buf) {
        byte[]  creatorBytes;
        
        creatorBytes = new byte[ValueCreator.BYTES];
        BufferUtil.get(buf, 0, creatorBytes, creatorBytes.length);
        return creatorBytes;
    }
    
    static ChecksumType getChecksumType(ByteBuffer buf) {
        return EnumValues.checksumType[buf.get(checksumTypeOffset)];
    }
    
    static byte[] getChecksum(ByteBuffer buf) {
        ChecksumType    checksumType;
        byte[]          checksum;
        
        checksumType = getChecksumType(buf);
        checksum = new byte[checksumType.length()];
        BufferUtil.get(buf, checksumOffset, checksum, checksumType.length());
        return checksum;
    }
    
    static ByteBuffer createSegmentMetaDataBuffer(byte[] creatorBytes, int storedLength, int uncompressedLength, 
                                                  ChecksumType checksumType, byte[] checksum) {
        ByteBuffer  segmentMetaDataBuffer;
        //Checksum    checksum;
        //ByteBuffer  checksumDest;
        
        //checksum = ChecksumProvider.getChecksum(checksumType);
        //System.out.printf("createSegmentMetaDataBuffer\t%s\t%d\t%d\n", 
        //        StringUtil.byteArrayToHexString(creatorBytes), storedLength, uncompressedLength);
        segmentMetaDataBuffer = ByteBuffer.allocate(segmentedValueBufferLength + checksumType.length());
        // FIXME - checksum the index data
        if (creatorBytes.length != ValueCreator.BYTES) {
            throw new RuntimeException("Unexpected creatorBytes.length != ValueCreator.BYTES");
        }
        segmentMetaDataBuffer.put(creatorBytes);
        segmentMetaDataBuffer.putInt(storedLength);
        segmentMetaDataBuffer.putInt(uncompressedLength);
        segmentMetaDataBuffer.put((byte)checksumType.ordinal());
        if (checksumType != ChecksumType.NONE) {
            segmentMetaDataBuffer.put(checksum);
        }
        
        //System.out.printf("%s %d\n", checksumType, checksum.length);
        //System.out.printf("%s %d\n", getChecksumType(segmentMetaDataBuffer), getChecksum(segmentMetaDataBuffer).length);
        //System.out.printf("%s\n", StringUtil.byteBufferToHexString((ByteBuffer)segmentMetaDataBuffer.asReadOnlyBuffer().position(0)));
        
        //System.out.println("source\t"+ StringUtil.byteBufferToHexString((ByteBuffer)segmentMetaDataBuffer.asReadOnlyBuffer().flip()));
        
        /*
        checksumDest = segmentMetaDataBuffer.slice();
        checksum.checksum((ByteBuffer)segmentMetaDataBuffer.asReadOnlyBuffer().flip(), 
                          checksumDest);
                          */
        
        //System.out.println("dest\t"+ StringUtil.byteBufferToHexString((ByteBuffer)checksumDest.asReadOnlyBuffer().flip()));
        
        //System.out.println(StringUtil.byteBufferToHexString(segmentMetaDataBuffer));
        //System.out.println(checksumSegmentMetaDataBuffer(segmentMetaDataBuffer, checksumType));
        
        return segmentMetaDataBuffer;
   }
    
    /*
     * Not needed since the regular checksum mechanism is in place
   static boolean checksumSegmentMetaDataBuffer(ByteBuffer buf, ChecksumType checksumType) {
       Checksum     checksum;
       ByteBuffer   bufToChecksum;
       ByteBuffer   checksumOfBuf;
       ByteBuffer   bufInternalChecksum;
       
       checksum = ChecksumProvider.getChecksum(checksumType);
       bufToChecksum = (ByteBuffer)buf.asReadOnlyBuffer().flip().limit(segmentedValueBufferLength);
       //System.out.println("bufToChecksum      \t"+ StringUtil.byteBufferToHexString((ByteBuffer)bufToChecksum.asReadOnlyBuffer().flip()));
       checksumOfBuf = ByteBuffer.wrap(checksum.checksum(bufToChecksum));
       bufInternalChecksum = (ByteBuffer)buf.asReadOnlyBuffer().position(segmentedValueBufferLength);
       //System.out.println("checksumOfBuf      \t"+ StringUtil.byteBufferToHexString(checksumOfBuf));
       //System.out.println("bufInternalChecksum\t"+ StringUtil.byteBufferToHexString(bufInternalChecksum));
       return checksumOfBuf.equals(bufInternalChecksum);
   }
   */
}
