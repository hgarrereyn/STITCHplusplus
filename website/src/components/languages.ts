// Languages of the tutorial, in picker order. The first one is shown until the reader picks one.
export const LANGUAGES = [
  ["python", "Python"],
  ["cpp", "C/C++"],
  ["javascript", "JavaScript"],
  ["java", "Java"],
  ["go", "Go"],
  ["rust", "Rust"],
  ["ruby", "Ruby"],
  ["swift", "Swift"],
  ["objc", "Objective-C"],
] as const;

export type LanguageId = (typeof LANGUAGES)[number][0];
