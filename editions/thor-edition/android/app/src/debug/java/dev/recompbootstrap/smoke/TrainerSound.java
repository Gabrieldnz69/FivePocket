package dev.recompbootstrap.smoke;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.util.Log;

/**
 * Thor edition (thor-radio v832; owner 2026-10-25: a sound when the Lester trainer button is tapped): assets/owner/trainer-open.m4a (his
 * own sound; OWNER BUILDS ONLY - without it nothing plays), preloaded into a SoundPool when the game window starts (low latency) and played
 * once per open by the trainer button. Game audio attributes: the media volume applies. The button is shown and takes taps only while the
 * game shows its HUD map (no pause screen) and the screen is on, so the sound never plays in a pause or with the screen off. Tag RecompThorMenu.
 */
final class TrainerSound {
    static final String ASSET="owner/trainer-open.m4a";
    private SoundPool pool;private int id;private volatile boolean loaded;
    TrainerSound(Context c){
        try(AssetFileDescriptor fd=c.getAssets().openFd(ASSET)){
            pool=new SoundPool.Builder().setMaxStreams(1).setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
            pool.setOnLoadCompleteListener((p,sample,status)->{loaded=status==0;Log.i(ThorMenus.TAG,"TRAINER_SOUND loaded="+(loaded?1:0));});
            id=pool.load(fd,1);
        }catch(java.io.IOException | RuntimeException none){pool=null;} // (no asset: a public build - silent)
    }
    /** Once per open (the trainer button's tap). */
    void play(){if(pool!=null && loaded)pool.play(id,1f,1f,1,0,1f);}
    void release(){if(pool!=null){pool.release();pool=null;}}
}
