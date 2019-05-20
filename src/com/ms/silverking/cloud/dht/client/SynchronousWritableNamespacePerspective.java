package com.ms.silverking.cloud.dht.client;

import java.util.Map;
import java.util.Set;

import com.ms.silverking.cloud.dht.InvalidationOptions;
import com.ms.silverking.cloud.dht.PutOptions;

/**
 * A namespace perspective that provides synchronous write operations. All operations block until completion.
 * 
 * @param <K> key type
 * @param <V> value type
 */
public interface SynchronousWritableNamespacePerspective<K,V> extends BaseNamespacePerspective<K,V> {
    /**
     * Multi-value Put operation
     * @param values map of key-value pairs to store
     * @param putOptions options for the Put operation
     * @throws PutException
     */
	public void put(Map<? extends K, ? extends V> values, PutOptions putOptions) throws PutException;
	/**
     * Multi-value Put operation using default PutOptions.
     * @param values map of key-value pairs to store
	 * @throws PutException
	 */
	public void put(Map<? extends K, ? extends V> values) throws PutException;
	/**
     * Single-value Put operation.
	 * @param key key to associate the value with
	 * @param value value to store
     * @param putOptions options for the Put operation
	 * @throws PutException
	 */
	public void put(K key, V value, PutOptions putOptions) throws PutException;
	/**
     * Single-value Put operation using default PutOptions.
     * @param key key to associate the value with
     * @param value value to store
	 * @throws PutException
	 */
	public void put(K key, V value) throws PutException;
	

    /**
     * Multi-value Invalidation operation
     * @param keys keys to invalidate
     * @param invalidationOptions options for the Put operation
     * @throws InvalidationException
     */
	public void invalidate(Set<? extends K> keys, InvalidationOptions invalidationOptions) throws InvalidationException;
	/**
     * Multi-value Invalidation operation using default PutOptions.
     * @param keys keys to invalidate
	 * @throws InvalidationException
	 */
	public void invalidate(Set<? extends K> keys) throws InvalidationException;
	/**
     * Single-value Invalidation operation.
	 * @param key key to invalidate
     * @param invalidationOptions options for the Invalidation operation
	 * @throws InvalidationException
	 */
	public void invalidate(K key, InvalidationOptions invalidationOptions) throws InvalidationException;
	/**
     * Single-value Invalidation operation using default InvalidationOptions.
     * @param key key to invalidate
	 * @throws InvalidationException
	 */
	public void invalidate(K key) throws InvalidationException;
	
	/*
	 * snapshots deprecated for now	
    public void snapshot(long version) throws SnapshotException;
    public void snapshot() throws SnapshotException;
    */
    
	/*
    // temp dev only (probably)
    public void syncRequest(long version) throws SyncRequestException;
    public void syncRequest() throws SyncRequestException;
    */
}
