package com.ms.silverking.cloud.ring;


public class LongConcurrentMapRing<T> {//implements Ring<Long, T> {
	/*
	private final Mode	mode;
	private final ConcurrentSkipListMap<Long, T>	ringMap;
	
	public enum Mode {SUBSEQUENT, ROTATE};

	public LongConcurrentMapRing(Mode mode) {
		this.mode = mode;
		ringMap = new ConcurrentSkipListMap<Long, T>();
	}
	
	@Override
	public void put(Long key, T member) {
		ringMap.put(key, member);
	}
	
	private Map.Entry<Long, T> getEntry(Long key) {
		Map.Entry<Long, T> entry;
		
		entry = ringMap.ceilingEntry(key);
		if (entry == null) {
			entry = ringMap.firstEntry();
		}
		return entry;
	}
	
	@Override
	public T getOwner(Long key) {
		Map.Entry<Long, T> entry;

		entry = getEntry(key);
		return entry.getValue();
	}
	
	@Override
	public List<T> get(Long key, int numMembers) {
		Map.Entry<Long, T> entry;
		List<T>	members;
		
		if (numMembers < 1) {
			throw new RuntimeException("numMembers < 1");
		}
		members = new ArrayList<T>(numMembers);
		entry = getEntry(key);
		members.add(entry.getValue());
		switch (mode) {
		case SUBSEQUENT: addSubsequent(members, entry, numMembers - 1); break;
		case ROTATE: addRotated(members, entry, numMembers - 1); break;
		default: throw new RuntimeException("panic");
		}
		return members;
	}
	
	private void addSubsequent(List<T> members, Map.Entry<Long,T> entry, int numSubsequent) {
		for (int i = 0; i < numSubsequent; i++) {
			entry = ringMap.higherEntry(entry.getKey());
			if (entry == null) {
				entry = ringMap.firstEntry();
			}
			members.add(entry.getValue());
		}
	}
	
	private void addRotated(List<T> members, Map.Entry<Long,T> entry, int numSubsequent) {
		for (int i = 0; i < numSubsequent; i++) {
			Long	rotated;
			
			rotated = Long.rotateRight(entry.getKey(), 1);
			entry = getEntry(rotated);
			members.add(entry.getValue());
		}
	}
	
	@Override
	public Collection<T> getMembers() {
		return ringMap.values();
	}
	
	@Override
	public int numMembers() {
		return ringMap.size();
	}
	*/
}
