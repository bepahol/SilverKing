package com.ms.silverking.cloud.dht.client;

import java.util.Map;

/**
 * An asynchronous retrieval that supports raw value access. May be polled for partial results.
 * @param <K> key type
 * @param <V> value type
 */
public interface AsyncValueRetrieval<K,V> extends AsyncRetrieval<K,V> {
	/**
	 * Returns raw values for all successfully complete retrievals. 
	 * @return
	 * @throws RetrievalException
	 */
	public Map<K,V> getValues() throws RetrievalException;
	/**
	 * Returns the raw value for the given key if it is present. 
	 * @return
	 * @throws RetrievalException
	 */
	public V getValue(K key) throws RetrievalException;	
	/**
	 * Returns raw values for all successfully complete retrievals that
	 * have completed since the last call to this method and getLatestStoredValues(). 
	 * Each successfully retrieved value will be reported exactly once by this 
	 * method and getLatestStoredValues().
	 * This method is unaffected by calls to either getValues() or getStoredValues().
	 * Concurrent execution is permitted, but the precise values returned are
	 * undefined.  
	 * @return
	 * @throws RetrievalException
	 */
	public Map<K, V> getLatestValues() throws RetrievalException;
	
}
