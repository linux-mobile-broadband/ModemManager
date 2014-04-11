
const Mainloop = imports.mainloop;
const GLib     = imports.gi.GLib;

const modemWatcher = imports.modemWatcher;

function start() {
    let watcher = new modemWatcher.ModemWatcher();

    Mainloop.run();
}
