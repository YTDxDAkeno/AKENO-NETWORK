// AKENO Audio Lab 3.1 (EXPERIMENTAL, isolated audio-output experiment): entry for the websrv-format
// homebrew launcher. "daemon: true" hands the ELF to the ELF loader as a background payload (the
// same process type as the AKENO daemon); the launcher does not close a running game for it. It
// announces, waits five seconds, plays one default tone, then exits. It opens no microphone, uses
// no network, has no control-file path, and writes only to /data/akeno-audiolab/.
async function main() {
  return {
    mainText: 'AKENO Audio Lab 3.1',
    secondaryText: 'Isolated PS5 audio-output experiment (experimental)',
    onclick: async () => {
      return { path: window.workingDir + '/akeno-audio-lab.elf', daemon: true };
    },
  };
}
