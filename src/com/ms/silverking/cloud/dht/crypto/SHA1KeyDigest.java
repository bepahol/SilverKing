package com.ms.silverking.cloud.dht.crypto;

import java.security.MessageDigest;

import com.ms.silverking.cloud.dht.client.impl.KeyDigest;
import com.ms.silverking.cloud.dht.common.DHTKey;
import com.ms.silverking.cloud.dht.common.SimpleKey;
import com.ms.silverking.numeric.NumConversion;

public class SHA1KeyDigest implements KeyDigest {
    public SHA1KeyDigest() {
    }
    
    @Override
    public DHTKey computeKey(byte[] bytes) {
        MessageDigest   md;
        
        md = SHA1Digest.getLocalMessageDigest();
        md.update(bytes, 0, bytes.length);
        // FUTURE - use all sha1 bytes
        return SimpleKey.mapToSimpleKey(md.digest());
    }
    
    @Override
    public byte[] getSubKeyBytes(DHTKey key, int subKeyIndex) {
        byte[]  keyBytes;
        
        // FUTURE - use all sha1 bytes
        keyBytes = new byte[SimpleKey.BYTES_PER_KEY];
        NumConversion.longToBytes(key.getMSL(), keyBytes, 0);
        NumConversion.longToBytes(key.getLSL() + subKeyIndex, keyBytes, NumConversion.BYTES_PER_LONG);
        return keyBytes;
    }
    
    @Override
    public DHTKey[] createSubKeys(DHTKey key, int numSubKeys) {
        DHTKey[]    subKeys;
        
        subKeys = new DHTKey[numSubKeys];
        for (int i = 0; i < subKeys.length; i++) {
            subKeys[i] = computeKey(getSubKeyBytes(key, i));
        }
        return subKeys;
    }
}
