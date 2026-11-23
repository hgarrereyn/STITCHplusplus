import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

export default defineConfig({
  site: "https://hgarrereyn.github.io",
  base: "/STITCHplusplus",
  integrations: [
    starlight({
      title: "STITCH++",
      description: "API fuzzing for libraries in nine languages, built for coding agents.",
      social: [{ icon: "github", label: "GitHub", href: "https://github.com/hgarrereyn/STITCHplusplus" }],
      editLink: { baseUrl: "https://github.com/hgarrereyn/STITCHplusplus/edit/main/website/" },
      customCss: [
        "@fontsource-variable/inter",
        "@fontsource-variable/jetbrains-mono",
        "./src/styles/custom.css",
        "./src/styles/landing.css",
      ],
      expressiveCode: {
        themes: ["github-dark-default", "github-light-default"],
        // No window chrome: shell commands and output read as plain blocks.
        defaultProps: { frame: "none" },
        styleOverrides: {
          borderRadius: "0.625rem",
          borderColor: "var(--sl-color-hairline-light)",
          codeBackground: "var(--stitch-surface)",
          codeFontFamily: "var(--sl-font-mono)",
          codeFontSize: "0.8125rem",
          codeLineHeight: "1.7",
          codePaddingBlock: "0.875rem",
          codePaddingInline: "1.125rem",
          uiFontFamily: "var(--sl-font)",
          frames: {
            frameBoxShadowCssValue: "none",
            editorBackground: "var(--stitch-surface)",
            editorTabBarBackground: "var(--stitch-surface)",
            editorActiveTabBackground: "var(--stitch-surface)",
            editorActiveTabIndicatorTopColor: "transparent",
            editorActiveTabIndicatorBottomColor: "var(--sl-color-accent)",
            editorTabBarBorderBottomColor: "var(--sl-color-hairline-light)",
            terminalBackground: "var(--stitch-surface)",
            terminalTitlebarBackground: "var(--stitch-surface)",
            terminalTitlebarBorderBottomColor: "var(--sl-color-hairline-light)",
            terminalTitlebarDotsOpacity: "0",
            inlineButtonBackground: "var(--sl-color-gray-5)",
            inlineButtonBorder: "var(--sl-color-hairline-light)",
            tooltipSuccessBackground: "var(--sl-color-accent)",
          },
        },
      },
      head: [
        // Restore the tutorial language before the page paints.
        {
          tag: "script",
          content:
            "try{var l=localStorage.getItem('stitch-lang');if(l)document.documentElement.dataset.stitchLang=l}catch(e){}",
        },
      ],
      components: {
        PageTitle: "./src/components/PageTitle.astro",
      },
      sidebar: [
        {
          label: "For humans",
          items: [
            { label: "Overview", slug: "guide" },
            { label: "Install", slug: "guide/install" },
            {
              label: "Tutorial: a key-value store",
              items: [
                { label: "1. The library", slug: "guide/tutorial/library" },
                { label: "2. Create a project", slug: "guide/tutorial/project" },
                { label: "3. Write stubs", slug: "guide/tutorial/stubs" },
                { label: "4. Build and probe", slug: "guide/tutorial/probe" },
                { label: "5. Checks and data types", slug: "guide/tutorial/checks" },
                { label: "6. Fuzz and triage", slug: "guide/tutorial/triage" },
                { label: "7. Reproducers and coverage", slug: "guide/tutorial/reproducers" },
              ],
            },
            { label: "Concepts", slug: "guide/concepts" },
          ],
        },
        {
          label: "For agents",
          items: [
            { label: "Start here", slug: "agents" },
            { label: "Workflow", slug: "agents/workflow" },
            { label: "Writing stubs", slug: "agents/stubs" },
            { label: "Reading the probe report", slug: "agents/probe" },
            { label: "Triage", slug: "agents/triage" },
          ],
        },
        {
          label: "Reference",
          items: [
            { label: "CLI", slug: "reference/cli" },
            { label: "Languages", collapsed: true, items: [{ autogenerate: { directory: "reference/languages" } }] },
          ],
        },
      ],
    }),
  ],
});
