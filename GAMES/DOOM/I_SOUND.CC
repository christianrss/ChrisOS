/* Sound and music are no-ops on ChrisC / CLVM. */

int snd_sfxdevice;
int snd_musicdevice;
int snd_samplerate;
int snd_cachesize;
int snd_maxslicetime_ms;
char *snd_musiccmd;

void I_InitSound(int use_sfx_prefix) {
    snd_sfxdevice = 0;
    snd_musicdevice = 0;
    snd_samplerate = 11025;
    snd_cachesize = 0;
    snd_maxslicetime_ms = 28;
    snd_musiccmd = "";
    use_sfx_prefix = use_sfx_prefix;
}

void I_ShutdownSound() {
}

int I_GetSfxLumpNum(int *sfxinfo) {
    sfxinfo = sfxinfo;
    return -1;
}

void I_UpdateSound() {
}

void I_UpdateSoundParams(int channel, int vol, int sep) {
    channel = channel;
    vol = vol;
    sep = sep;
}

int I_StartSound(int *sfxinfo, int channel, int vol, int sep) {
    sfxinfo = sfxinfo;
    channel = channel;
    vol = vol;
    sep = sep;
    return -1;
}

void I_StopSound(int channel) {
    channel = channel;
}

int I_SoundIsPlaying(int channel) {
    channel = channel;
    return 0;
}

void I_PrecacheSounds(int *sounds, int num_sounds) {
    sounds = sounds;
    num_sounds = num_sounds;
}

void I_InitMusic() {
}

void I_ShutdownMusic() {
}

void I_SetMusicVolume(int volume) {
    volume = volume;
}

void I_PauseSong() {
}

void I_ResumeSong() {
}

int I_RegisterSong(char *data, int len) {
    data = data;
    len = len;
    return 0;
}

void I_UnRegisterSong(int handle) {
    handle = handle;
}

void I_PlaySong(int handle, int looping) {
    handle = handle;
    looping = looping;
}

void I_StopSong() {
}

int I_MusicIsPlaying() {
    return 0;
}

void I_BindSoundVariables() {
}
