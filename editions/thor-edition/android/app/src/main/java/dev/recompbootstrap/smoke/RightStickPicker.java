package dev.recompbootstrap.smoke;

/**
 * Discord D5 (2026-11-10, "I cannot move and rotate the screen using the xbox control"): which axis pair is the right stick.
 * The old rule was "AXIS_Z / AXIS_RZ when both exist, else AXIS_RX / AXIS_RY". That is right for a Bluetooth Xbox pad and a
 * DualShock with Android's key layout file, but a USB xinput pad (or a PlayStation pad) without a layout file reports its
 * TRIGGERS as Z / RZ: Android scales those as centred axes (-1..1, resting at -1) and puts the right stick on RX / RY. The old
 * rule then read the triggers as the right stick: the stick did nothing and the camera followed the triggers.
 *
 * The right stick is the pair that looks like a stick and rests at its centre: a joystick-source range that is symmetric, not
 * an axis seen resting at its low end (a trigger). Before any value is seen, a pair with flat / fuzz (Linux xpad gives its sticks
 * flat and fuzz and its triggers none) beats one without; then Android's standard gamepad mapping (Z / RZ). Once enough events
 * are seen (Rest), the pair actually resting at the centre decides. When the right stick is RX / RY and the pad has no trigger
 * axis of its own (LTRIGGER / RTRIGGER / BRAKE / GAS), Z / RZ are its triggers. Pure (no Android types): host tested.
 */
final class RightStickPicker {
    static final int PAIR_NONE=0, PAIR_Z_RZ=1, PAIR_RX_RY=2;
    static final int Z=0, RZ=1, RX=2, RY=3;           // indexes into the candidate arrays (Android: AXIS_Z 11, AXIS_RZ 14, AXIS_RX 12, AXIS_RY 13)
    static final int UNKNOWN=0, CENTRE=1, LOW=2;     // where an axis was seen resting

    private RightStickPicker() {}

    /** One axis as Android reports it (InputDevice.MotionRange); null where the device has no such axis. */
    static final class Axis {
        final float min, max, flat, fuzz;
        final boolean joystick;                        // the range's source includes SOURCE_JOYSTICK
        Axis(float min, float max, float flat, float fuzz, boolean joystick) {
            this.min=min; this.max=max; this.flat=flat; this.fuzz=fuzz; this.joystick=joystick;
        }
        boolean symmetric() { final float span=max-min; return span>0f && Math.abs(max+min)<=0.1f*span; }
        boolean flatOrFuzz() { return flat>0f || fuzz>0f; }
    }

    /** Where each candidate axis rests: counts of events near its centre and near its low end. */
    static final class Rest {
        static final int SAMPLES=8;                    // events before the rest is believed
        private final int[] total=new int[4], centre=new int[4], low=new int[4];
        void observe(int index, float value, Axis axis) {
            if(axis==null) return;
            final float span=axis.max-axis.min; if(!(span>0f)) return;
            total[index]++;
            if(Math.abs(value-(axis.max+axis.min)/2f)<=0.15f*span/2f) centre[index]++;
            else if(value<=axis.min+0.075f*span) low[index]++;
        }
        int of(int index) {
            if(total[index]<SAMPLES) return UNKNOWN;
            if(2*centre[index]>=total[index]) return CENTRE;
            if(2*low[index]>=total[index]) return LOW;
            return UNKNOWN;
        }
        /** True once every present candidate has been seen SAMPLES times. */
        boolean decided(Axis[] candidates) {
            boolean any=false;
            for(int i=0;i<4;++i) if(candidates[i]!=null) { any=true; if(total[i]<SAMPLES) return false; }
            return any;
        }
        void clear() { java.util.Arrays.fill(total,0); java.util.Arrays.fill(centre,0); java.util.Arrays.fill(low,0); }
    }

    static boolean stickLike(Axis axis, int rest) { return axis!=null && axis.joystick && axis.symmetric() && rest!=LOW; }

    /** The right stick's pair. candidates: Z, RZ, RX, RY (null = absent); rest: null before any event. */
    static int pick(Axis[] candidates, Rest rest) {
        final int rz=rest==null?UNKNOWN:rest.of(Z), rrz=rest==null?UNKNOWN:rest.of(RZ);
        final int rrx=rest==null?UNKNOWN:rest.of(RX), rry=rest==null?UNKNOWN:rest.of(RY);
        final boolean zOk=stickLike(candidates[Z],rz) && stickLike(candidates[RZ],rrz);
        final boolean rOk=stickLike(candidates[RX],rrx) && stickLike(candidates[RY],rry);
        if(zOk && rOk) {
            final int zCentre=(rz==CENTRE?1:0)+(rrz==CENTRE?1:0), rCentre=(rrx==CENTRE?1:0)+(rry==CENTRE?1:0);
            if(zCentre!=rCentre) return zCentre>rCentre?PAIR_Z_RZ:PAIR_RX_RY;
            final boolean zFlat=candidates[Z].flatOrFuzz() && candidates[RZ].flatOrFuzz();
            final boolean rFlat=candidates[RX].flatOrFuzz() && candidates[RY].flatOrFuzz();
            if(zFlat!=rFlat) return zFlat?PAIR_Z_RZ:PAIR_RX_RY;
            return PAIR_Z_RZ;
        }
        return zOk?PAIR_Z_RZ:rOk?PAIR_RX_RY:PAIR_NONE;
    }

    /** True when Z / RZ are the pad's triggers: the right stick is RX / RY, both exist, and the pad has no trigger axis of its own. */
    static boolean zTriggers(int pair, Axis[] candidates, boolean ownTriggerAxes) {
        return pair==PAIR_RX_RY && !ownTriggerAxes && candidates[Z]!=null && candidates[RZ]!=null;
    }

    static String name(int pair) { return pair==PAIR_Z_RZ?"Z/RZ":pair==PAIR_RX_RY?"RX/RY":"none"; }
}
