// AKENO NETWORK Native: entry for the websrv-format homebrew launcher (ps5-payload-dev/websrv).
// The launcher starts eboot.elf as the foreground application; "--launcher" tells AKENO Native
// that it was started this way (it then opens the screen, controller and audio).
// Note: starting any homebrew this way closes the game that is running (launcher behaviour).
async function main() {
  const payload = window.workingDir + '/eboot.elf';
  return {
    mainText: 'AKENO NETWORK',
    secondaryText: 'Native prototype 0.1',
    onclick: async () => {
      return { path: payload, args: ['--launcher'] };
    },
  };
}
