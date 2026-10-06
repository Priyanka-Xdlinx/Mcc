// Used by Jest only; Vite compiles the app itself.
module.exports = {
  presets: [
    ["@babel/preset-env", { targets: { node: "current" } }],
    ["@babel/preset-react", { runtime: "automatic" }],
  ],
  env: {
    test: {
      // Jest runs on CommonJS, which has no `import.meta`: Vite's
      // `import.meta.env.*` is rewritten to `process.env.*`.
      plugins: [
        function importMetaEnvToProcessEnv({ types: t }) {
          return {
            visitor: {
              MetaProperty(path) {
                const { node } = path;
                if (node.meta.name === "import" && node.property.name === "meta") {
                  path.replaceWith(t.identifier("process"));
                }
              },
            },
          };
        },
      ],
    },
  },
};
