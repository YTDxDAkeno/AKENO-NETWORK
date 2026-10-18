// AKENO NETWORK Native: entry for the websrv-format homebrew launcher (ps5-payload-dev/websrv).
// The launcher starts the ELF as the foreground application; "--launcher" tells AKENO Native that
// it was started this way (it then opens the screen, controller and audio), "--install-dir" where
// its folder is (for updates, docs/PS5_NATIVE_UPDATER.md).
// Note: starting any homebrew this way closes the game that is running (launcher behaviour).
//
// Which ELF starts: update/active.json, written only by AKENO's updater. eboot.elf (the version
// installed by hand) is the fallback for anything missing or unexpected. A newly installed version
// that has not confirmed itself after AKENO_MAX_ATTEMPTS starts is skipped: the previous version
// starts and records the rollback. Nothing here deletes or writes files.
const AKENO_MAX_ATTEMPTS = 2;
const AKENO_FACTORY_ELF = 'eboot.elf';
const AKENO_VERSION_ELF = /^update\/versions\/(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]{1,40})?\/eboot\.elf$/;

function akenoElfAllowed(elf) {
  return elf === AKENO_FACTORY_ELF || (typeof elf === 'string' && AKENO_VERSION_ELF.test(elf));
}

// The launch plan for an active.json object (or null): { elf, version, note }.
function akenoChoose(active) {
  const factory = { elf: AKENO_FACTORY_ELF, version: '', note: '' };
  if (!active || active.format !== 1 || !akenoElfAllowed(active.elf)) return factory;
  const attempts = Number.isInteger(active.attempts) ? active.attempts : 0;
  if (active.state === 'pending' && attempts >= AKENO_MAX_ATTEMPTS) {
    const prev = active.previous || {};
    const elf = akenoElfAllowed(prev.elf) ? prev.elf : AKENO_FACTORY_ELF;
    return { elf, version: prev.version || '', note: `${active.version} did not start; using the previous version` };
  }
  if (active.state !== 'pending' && active.state !== 'confirmed') return factory;
  return { elf: active.elf, version: active.version || '', note: active.state === 'pending' ? 'new version, first starts' : '' };
}

async function akenoReadActive(dir, readText) {
  try {
    const res = await readText(dir + '/update/active.json');
    if (!res || res.status !== 200 || !res.data) return null;
    return JSON.parse(res.data);
  } catch (e) {
    return null;
  }
}

function akenoLaunch(dir, elf) {
  return { path: dir + '/' + elf, args: ['--launcher', '--install-dir', dir] };
}

async function main() {
  const dir = window.workingDir;
  const readText = (p) => ApiClient.fsGetFileText(p);
  const active = await akenoReadActive(dir, readText);
  const plan = akenoChoose(active);
  const options = [];
  const prev = active && active.previous;
  if (prev && akenoElfAllowed(prev.elf) && prev.elf !== plan.elf) {
    options.push({
      text: 'Start the previous version' + (prev.version ? ' (' + prev.version + ')' : ''),
      onclick: async () => akenoLaunch(dir, prev.elf),
    });
  }
  if (plan.elf !== AKENO_FACTORY_ELF) {
    options.push({ text: 'Start the version installed by hand', onclick: async () => akenoLaunch(dir, AKENO_FACTORY_ELF) });
  }
  return {
    mainText: 'AKENO NETWORK',
    secondaryText: 'Native' + (plan.version ? ' ' + plan.version : '') + (plan.note ? ' - ' + plan.note : ''),
    onclick: async () => {
      // Read again at the moment of the start: an update may have been installed meanwhile.
      const now = akenoChoose(await akenoReadActive(dir, readText));
      return akenoLaunch(dir, now.elf);
    },
    options: options.length ? options : undefined,
  };
}

if (typeof module !== 'undefined') module.exports = { akenoChoose, akenoElfAllowed, akenoReadActive, akenoLaunch, main };
