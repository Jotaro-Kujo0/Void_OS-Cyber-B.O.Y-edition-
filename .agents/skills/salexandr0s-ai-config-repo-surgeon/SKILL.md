---
name: repo-surgeon
description: Deep structural audit and modernization skill for messy or prototype codebases. Use when the user wants a codebase audit, dead code detection, folder restructuring, naming cleanup, scalability risk analysis, worst-file rewrite, or a production-grade README. Triggers on "audit this repo", "clean up this codebase", "structural audit", "repo surgeon", "modernize this project", "production readiness check".
metadata:
  author: nationalbank
  version: "1.0.0"
---

# /repo-surgeon

## Mission
Transform a codebase from prototype-grade into production-grade by performing a **full structural audit**, identifying what should be deleted, renamed, reorganized, extracted, rewritten, and documented, then returning a prioritized modernization plan with evidence.

This skill is designed to be **safe, evidence-based, and execution-oriented**.

## Parameters

Pass parameters as arguments: `/repo-surgeon mode=patch scope=full strictness=balanced output=exhaustive`

| Parameter     | Values                                              | Default      | Description                                                    |
| ------------- | --------------------------------------------------- | ------------ | -------------------------------------------------------------- |
| `mode`        | `report`, `patch`, `rewrite`                        | `report`     | Analyze only, include diffs, or fully rewrite the worst file   |
| `scope`       | `full`, `frontend`, `backend`, `api`, `shared`, `tests` | `full`   | Which part of the codebase to audit                            |
| `strictness`  | `conservative`, `balanced`, `aggressive`            | `balanced`   | How aggressive recommendations should be                       |
| `output`      | `concise`, `standard`, `exhaustive`                 | `standard`   | Detail level of the report                                     |
| `risk_target` | any string                                          | `10000 DAU`  | Scale target for scalability analysis                          |

---

## When to use this skill
- Full codebase audit
- Dead code detection and safe deletion candidates
- Folder restructuring or feature-based architecture proposals
- Hardcoded value extraction and configuration cleanup
- Naming cleanup and standardization
- Scalability risk analysis
- A rewrite of the single worst file
- A polished README and onboarding docs
- A practical plan to turn a messy repo into a serious project

---

## Core operating rules
1. **Never hallucinate repository facts.** Base every claim on files actually provided.
2. **Cite evidence.** Every important finding must reference file paths and, when possible, symbols and line numbers.
3. **Separate certainty levels.** Label findings as `proven`, `high-confidence`, or `needs verification`.
4. **Prefer safe deletion over risky deletion.** Never recommend deleting files used by:
   - dynamic imports
   - framework conventions (routes, pages, loaders, middleware, layouts)
   - reflection/registration patterns
   - config/CLI entrypoints
   - side-effect imports
   unless there is concrete evidence they are unused.
5. **Preserve behavior by default.** Refactors should improve structure without changing the public API or runtime behavior unless explicitly requested.
6. **Do not centralize secrets in source files.** Secrets belong in environment variables, not `config.ts`.
7. **Do not force a feature-folder structure when the repo is better served by package/module boundaries.** For monorepos, libraries, infra repos, SDKs, and shared platforms, propose the architecture that best fits the codebase rather than blindly applying one pattern.
8. **Optimize for maintainability-per-unit-of-churn.** Recommend the smallest set of changes with the highest long-term payoff.
9. **Be explicit about migration order.** Major restructures must include a low-risk rollout plan.
10. **When rewriting code, keep it production-quality.** Include types where appropriate, clear names, robust error handling, and tests or test notes.

---

## Full workflow

### Phase 0 — Repository intake
First, inspect the codebase and produce a quick inventory:
- language(s), framework(s), runtime(s)
- app type (SPA, SSR, API, CLI, library, monorepo, mobile, full-stack)
- package manager and build system
- entrypoints
- routing model
- state management/data layer
- API boundary points
- test setup
- environment/config strategy
- CI/CD clues

Then build a concise **repo map**:
- important directories
- generated directories to ignore
- likely feature boundaries
- hot spots (large files, generic names, duplicate utilities, root clutter)

Ignore common generated/vendor content unless directly relevant:
- `node_modules`
- build outputs
- coverage folders
- `.next`, `dist`, `out`, `target`, `.turbo`, etc.

---

### Phase 1 — Dead code removal
Audit the repo for code that is likely removable.

#### Detect
- unused imports
- unused exports
- unreferenced functions, classes, hooks, types, and helpers
- duplicate components/utilities that overlap substantially
- unreachable branches or dead conditionals
- orphaned files never imported, referenced, routed to, or executed
- stale test fixtures and obsolete mocks

#### Safety checks
Before recommending deletion, check for:
- barrel exports
- dynamic imports
- route conventions
- plugin registration patterns
- code generation inputs
- runtime reflection
- CLI/discovery-based loading

#### Output requirements
Produce a **Deletion Candidates** section with columns:
- item
- type (`import`, `function`, `type`, `component`, `file`, `branch`)
- location
- evidence
- confidence
- risk if removed
- recommendation (`delete`, `merge`, `verify first`)

Where possible, group by file and estimate the cleanup impact.

---

### Phase 2 — Structure and folder redesign
Evaluate whether the current layout is helping or hurting maintainability.

#### Diagnose structural issues
Look for:
- folders organized only by file type with no domain boundaries
- root-level sprawl
- cross-feature imports
- circular dependencies
- shared utilities that secretly belong to one feature
- mixed UI/business/data concerns in the same folder
- giant `components/`, `utils/`, or `services/` dumping grounds

#### Propose the best-fit target architecture
Choose one of these only if supported by the repo:
- feature-first modular structure
- bounded-context/domain modules
- layered architecture for libraries/services
- package boundaries for monorepos

#### Output requirements
Provide:
1. **Current structure diagnosis**
2. **Proposed target structure**
3. **Before/after tree**
4. **Import boundary rules**
5. **Migration order** that minimizes breakage

When proposing feature folders, prefer a shape like:

```text
src/
  features/
    auth/
      components/
      hooks/
      api/
      model/
      utils/
      types.ts
      index.ts
    billing/
    dashboard/
  shared/
    ui/
    lib/
    config/
    types/
  app/
    routes/
    providers/
    store/
```

But adapt the structure to the codebase rather than forcing this exact tree.

---

### Phase 3 — Hardcoded value extraction
Audit hardcoded values and extract them into the right abstraction.

#### Find
- UI strings repeated across files
- API base URLs and endpoint fragments
- timeout values, retry counts, debounce intervals
- magic numbers
- color literals and spacing tokens
- duplicated regex patterns
- feature flags
- local storage keys / cookie names
- repeated query keys and cache keys

#### Placement rules
Do **not** dump everything into one giant `config.ts`.
Use this rule instead:
- **Secrets and deployment-specific values** → `.env` / secret manager / runtime config
- **App-wide constants** → `src/shared/config/` or `src/shared/constants/`
- **Feature-specific constants** → colocated inside each feature module
- **Design tokens** → theme/tokens file
- **Copy/UI text** → messages/constants layer if repetition or localization value exists

#### Output requirements
Provide a **Constant Extraction Map** with:
- literal value
- current location(s)
- category
- target file
- exported name
- rationale

---

### Phase 4 — Naming standardization
Audit naming quality across:
- files, folders, variables, functions
- types/interfaces, hooks, components
- API clients, state slices/stores, tests

#### Flag vague names such as
`temp`, `data`, `handler`, `utils2`, `stuff`, `thing`, `item`, `helpers`, `misc`, `manager`, `service` (without domain meaning)

#### Naming rules
Recommend names that are:
- domain-specific
- intention-revealing
- scoped to their responsibility
- consistent with framework conventions
- accurate about side effects (`get`, `load`, `fetch`, `create`, `update`, `sync`, etc.)

#### Output requirements
Produce a **Rename Map** with:
- current name
- proposed name
- kind
- location
- reason
- migration notes if rename is public or high-impact

---

### Phase 5 — Scalability risks
Estimate what is most likely to fail first when usage reaches the target scale.
Default target: **10,000 daily active users**, unless overridden.

#### Assess likely failure points such as
- N+1 requests or chatty client-server patterns
- missing caching
- heavy client bundles
- synchronous/blocking work on hot paths
- poor DB query patterns
- unbounded memory/state growth
- missing pagination/virtualization
- brittle retry/error handling
- duplicated network calls
- no rate limiting / backpressure / queueing
- auth/session bottlenecks
- unoptimized image or asset loading
- absence of observability

#### Output requirements
List the **Top 5 scalability risks**, and for each provide:
- title
- evidence from the code
- failure mode at scale
- user-visible symptom
- severity
- likelihood
- recommended fix
- code-level example or pseudo-diff

Make this section grounded in the actual repo, not generic boilerplate.

---

### Phase 6 — Worst file rewrite
Identify the single file with the worst combination of:
- size, complexity, mixed responsibilities
- naming quality, duplication, side effects
- missing typing, weak error handling, low testability

#### Selection rule
Explain *why* it is the worst file before rewriting it.

#### Rewrite requirements
Produce a complete rewrite that:
- preserves current behavior unless a bugfix is explicitly justified
- separates concerns where practical
- uses strong names
- introduces types where applicable
- improves error handling
- reduces nesting and duplication
- extracts helper functions only when they improve clarity
- adds concise comments for non-obvious decisions
- includes follow-up test recommendations

#### Output requirements
Provide:
1. chosen file and rationale
2. key problems in the original
3. rewritten file
4. notable improvements
5. any dependent changes required in callers/imports

---

### Phase 7 — Documentation upgrade
Generate or rewrite a production-grade `README.md`.

#### README must cover
- what the project does
- who it is for
- key features
- tech stack
- prerequisites
- installation
- environment variable setup
- local development commands
- test/lint/build commands
- folder structure overview
- architecture notes
- deployment/run notes if inferable
- known limitations / next steps
- troubleshooting section if appropriate

If environment variables are referenced in code, generate an **`.env.example` spec** section listing:
- variable name, required/optional, purpose, example placeholder value

---

## Output format
Unless the user requests otherwise, return results in this order:

1. **Executive Summary** — biggest problems, biggest wins, overall maturity assessment
2. **Repository Inventory** — stack, architecture, entrypoints, key directories
3. **Deletion Candidates**
4. **Structure Redesign** — diagnosis, before/after tree, migration plan
5. **Constant Extraction Map**
6. **Rename Map**
7. **Top 5 Scalability Risks**
8. **Worst File Rewrite**
9. **README.md draft**
10. **30/60/90 minute action plan**
    - 30 min: safe cleanup
    - 60 min: structural wins
    - 90+ min: higher-churn refactors
11. **Optional Patch Set** (when mode is `patch` or `rewrite`) — diffs or exact file contents

---

## Evidence quality rubric
- **Proven** — directly evidenced by repository code or config
- **High-confidence** — strongly implied by patterns in the repository
- **Needs verification** — plausible but blocked by missing files or dynamic behavior

---

## Style requirements
- Be blunt but precise.
- Prefer concrete recommendations over generic advice.
- Use file paths and symbols often.
- Keep tone senior, technical, and execution-focused.
- Do not pad the answer with obvious best practices.
- When you do recommend a change, say exactly where it goes.

---

## Failure handling
If the repository is partial or incomplete:
- state what is missing
- continue with the strongest grounded analysis possible
- clearly separate repo-grounded findings from assumptions

If the repo is too large to analyze in one pass:
- prioritize entrypoints, feature boundaries, largest files, duplicated patterns, and high-risk modules
- explicitly state which areas were sampled vs fully inspected
