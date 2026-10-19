// AKENO Party Voice (EXPERIMENTAL): entry for the websrv-format homebrew launcher.
// "daemon: true" makes the launcher hand the ELF to its ELF loader as a background payload
// (websrv 7748df9, ApiClient.launchApp), the same process type as the AKENO daemon: the launcher
// does not close a running game for it. The service then waits for the AKENO plugin (Party > Voice
// chat); nothing is opened until you join voice. It refuses to run twice.
async function main() {
  return {
    mainText: 'AKENO Party Voice',
    secondaryText: 'Party voice while you play (experimental)',
    onclick: async () => {
      return { path: window.workingDir + '/akeno-party-voice.elf', daemon: true };
    },
  };
}
