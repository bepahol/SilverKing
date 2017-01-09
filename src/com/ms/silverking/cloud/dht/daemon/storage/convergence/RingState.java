package com.ms.silverking.cloud.dht.daemon.storage.convergence;

/**
 * A single-replica's state w.r.t. a given ring
 */
public enum RingState {
    INITIAL, 
    READY_FOR_CONVERGENCE_1, 
    READY_FOR_CONVERGENCE_2, 
    LOCAL_CONVERGENCE_COMPLETE_1, 
    ALL_CONVERGENCE_COMPLETE_1, 
    ALL_CONVERGENCE_COMPLETE_2, 
    CLOSED,
    ABANDONED;
    
    public boolean isFinal() {
    	return this == CLOSED || this == ABANDONED;
    }

    public boolean isValidTransition(RingState newRingState) {
        switch (this) {
        case INITIAL: return newRingState != INITIAL;
        case ABANDONED: // fall through
        case CLOSED: return false;
        default: return newRingState.ordinal() == this.ordinal() + 1 || newRingState == ABANDONED;
        }
    }
    
    public boolean requiresPassiveParticipation() {
        switch (this) {
        case CLOSED: return true;
        default: return false;
        }
    }
    
    public static RingState valueOf(byte[] b) {
        if (b != null && b.length > 0) {
            return valueOf(new String(b));
        } else {
            return null;
        }
        /*
        if (b != null && b.length == 1 && b[0] < values().length) {
            return values()[b[0]];
        } else {
            return null;
        }
        */
    }

    public boolean metBy(RingState nodeState) {
        if (nodeState == this) {
            return true;
        } else {
            switch (this) {
            case READY_FOR_CONVERGENCE_1:
                return nodeState == READY_FOR_CONVERGENCE_2 || nodeState == LOCAL_CONVERGENCE_COMPLETE_1
                        || nodeState == ALL_CONVERGENCE_COMPLETE_1 || nodeState == ALL_CONVERGENCE_COMPLETE_2
                        || nodeState == CLOSED;
            case READY_FOR_CONVERGENCE_2:
                return nodeState == LOCAL_CONVERGENCE_COMPLETE_1
                        || nodeState == ALL_CONVERGENCE_COMPLETE_1 || nodeState == ALL_CONVERGENCE_COMPLETE_2
                        || nodeState == CLOSED;
            case LOCAL_CONVERGENCE_COMPLETE_1:
                return nodeState == ALL_CONVERGENCE_COMPLETE_1 || nodeState == ALL_CONVERGENCE_COMPLETE_2
                        || nodeState == CLOSED;
            case ALL_CONVERGENCE_COMPLETE_1:
                return nodeState == ALL_CONVERGENCE_COMPLETE_2 || nodeState == CLOSED;
            case ALL_CONVERGENCE_COMPLETE_2:
                return nodeState == CLOSED;
            default: return false;
            }
        }
    }    
}
