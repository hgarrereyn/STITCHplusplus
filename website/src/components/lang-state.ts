// The reader's tutorial language: on <html data-stitch-lang>, remembered in localStorage.
import { LANGUAGES } from "./languages";

export const currentLang = (): string => document.documentElement.dataset.stitchLang ?? LANGUAGES[0][0];

export function setLang(id: string) {
  document.documentElement.dataset.stitchLang = id;
  try {
    localStorage.setItem("stitch-lang", id);
  } catch {}
  document.dispatchEvent(new CustomEvent("stitch-lang", { detail: id }));
}

export function onLang(fn: (id: string) => void) {
  document.addEventListener("stitch-lang", (e) => fn((e as CustomEvent<string>).detail));
}
