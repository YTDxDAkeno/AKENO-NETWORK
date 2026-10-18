import js from '@eslint/js';
import { defineConfig } from 'eslint/config';
import reactHooks from 'eslint-plugin-react-hooks';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default defineConfig(
  {
    ignores: [
      '**/dist/**',
      'clients/ps5/build*/**',
      'clients/ps5-native/build*/**',
      'clients/ps5-native/third_party/**',
      'clients/ps5-ingame/build*/**',
      'clients/ps5-ingame/third_party/**',
      '**/node_modules/**',
      '**/coverage/**',
      '**/playwright-report/**',
      '**/test-results/**',
      'apps/server/src/database/migrations/**',
    ],
  },
  js.configs.recommended,
  tseslint.configs.recommended,
  {
    files: ['**/*.{ts,tsx,js,mjs}'],
    languageOptions: {
      ecmaVersion: 2023,
      sourceType: 'module',
      globals: { ...globals.node },
    },
    rules: {
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      '@typescript-eslint/consistent-type-imports': ['error', { fixStyle: 'inline-type-imports' }],
      'no-console': 'off',
      eqeqeq: ['error', 'always'],
    },
  },
  {
    // Console page served by the PS5 app: a classic browser script for older WebKit builds.
    // The console page runs in the PS5 system browser: syntax newer than ES2017 is refused, so an
    // older WebKit can always parse it (the TypeScript parser itself accepts everything).
    files: ['clients/ps5/app/web/**/*.js'],
    languageOptions: { sourceType: 'script', globals: { ...globals.browser } },
    rules: {
      '@typescript-eslint/no-unused-vars': ['error', { caughtErrors: 'none' }],
      'no-restricted-syntax': [
        'error',
        { selector: 'ChainExpression', message: 'Optional chaining is newer than ES2017.' },
        { selector: "LogicalExpression[operator='??']", message: 'Nullish coalescing is newer than ES2017.' },
        { selector: "AssignmentExpression[operator=/^(\\?\\?|\\|\\||&&)=$/]", message: 'Logical assignment is newer than ES2017.' },
        { selector: 'ObjectExpression > SpreadElement', message: 'Object spread is newer than ES2017.' },
        { selector: 'ObjectPattern > RestElement', message: 'Object rest is newer than ES2017.' },
        { selector: 'CatchClause[param=null]', message: 'Optional catch binding is newer than ES2017.' },
        { selector: 'ForOfStatement[await=true]', message: 'for await is newer than ES2017.' },
        { selector: 'PropertyDefinition, PrivateIdentifier, StaticBlock', message: 'Class fields are newer than ES2017.' },
        { selector: 'Literal[bigint]', message: 'BigInt is newer than ES2017.' },
      ],
    },
  },
  {
    // Launcher manifest of AKENO Native, evaluated by websrv's launcher page (which calls main()).
    files: ['clients/ps5-native/package/**/*.js'],
    languageOptions: { sourceType: 'script', globals: { ...globals.browser } },
    rules: { '@typescript-eslint/no-unused-vars': ['error', { varsIgnorePattern: '^main$' }] },
  },
  {
    files: ['apps/web/**/*.{ts,tsx}'],
    languageOptions: { globals: { ...globals.browser } },
    extends: [reactHooks.configs.flat.recommended],
  },
  {
    // AudioWorklet modules run in the audio worklet global scope.
    files: ['apps/web/src/**/*-worklet.js'],
    languageOptions: { globals: { ...globals.audioWorklet } },
  },
  {
    files: ['**/test/**/*.ts', '**/tests/**/*.{ts,tsx}', '**/*.test.{ts,tsx}'],
    rules: {
      '@typescript-eslint/no-explicit-any': 'off',
      '@typescript-eslint/no-non-null-assertion': 'off',
    },
  },
);
