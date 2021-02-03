const el = document.querySelector("#count");
let count = 0;

setInterval(() => {
  count++;
  el.innerHTML = count;
}, 1000);