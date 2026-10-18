// AKENO Voice Lab (EXPERIMENTAL diagnostics): entry for the websrv-format homebrew launcher.
// "daemon: true" makes the launcher hand the ELF to its ELF loader as a background payload
// (websrv 7748df9, ApiClient.launchApp), the same process type as the AKENO daemon: the launcher
// does not close a running game for it. The lab ends by itself and writes
// /data/akeno-voicelab/report.txt.
async function main() {
  return {
    mainText: 'AKENO Voice Lab',
    secondaryText: 'Background audio tests (experimental)',
    onclick: async () => {
      return { path: window.workingDir + '/akeno-voicelab.elf', daemon: true };
    },
  };
}
