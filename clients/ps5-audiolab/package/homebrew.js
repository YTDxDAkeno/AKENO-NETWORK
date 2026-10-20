// AKENO Audio Lab 3.1 (EXPERIMENTAL, isolated audio-output experiment): entry for the websrv-format
// homebrew launcher. "daemon: true" hands the ELF to the ELF loader as a background payload (the
// same process type as the AKENO daemon); the launcher does not close a running game for it. It
// plays a tone ONLY after you append a `tone` command to /data/akeno-audiolab/control, opens no
// microphone, uses no network, and writes only to /data/akeno-audiolab/.
async function main() {
  return {
    mainText: 'AKENO Audio Lab 3.1',
    secondaryText: 'Isolated PS5 audio-output experiment (experimental)',
    onclick: async () => {
      return { path: window.workingDir + '/akeno-audio-lab.elf', daemon: true };
    },
  };
}
