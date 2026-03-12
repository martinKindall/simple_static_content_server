let clockInterval = null;

function showTime() {
    if (clockInterval !== null) {
        return;
    }
    function update() {
        const now = new Date();
        const timeZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
        document.getElementById('time-display').textContent =
            now.toLocaleTimeString() + ' (' + timeZone + ')';
    }
    update();
    clockInterval = setInterval(update, 1000);
}
