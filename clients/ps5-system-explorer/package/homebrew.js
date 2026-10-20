// AKENO System Explorer 6.0 (EXPERIMENTAL, read-only diagnostics): entry for the websrv-format
// homebrew launcher. "daemon: true" makes the launcher hand the ELF to its ELF loader as a
// background payload (websrv 7748df9, ApiClient.launchApp), the same process type as the AKENO
// daemon: the launcher does not close a running game for it. The Explorer ends by itself, opens no
// audio port and no microphone, and writes its reports to /data/akeno-system-explorer/.
async function main() {
  return {
    mainText: 'AKENO System Explorer',
    secondaryText: 'Read-only PS5 diagnostics (experimental)',
    onclick: async () => {
      return { path: window.workingDir + '/akeno-system-explorer.elf', daemon: true };
    },
  };
}
