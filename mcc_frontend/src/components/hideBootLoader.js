/** Removes the static boot screen index.html painted. Safe to call more than once. */
export default function hideBootLoader() {
  if (typeof document === "undefined") return;
  document.getElementById("boot-loader")?.remove();
}
