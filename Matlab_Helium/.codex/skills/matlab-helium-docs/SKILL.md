---
name: matlab-helium-docs
description: Maintain project documentation for Matlab_Helium when the user asks to write, rewrite, beautify, sync, or audit README.md, usage guides, workflow descriptions, module explanations, or parameter documentation. Use when code structure, process order, module responsibilities, or path rules need to be reflected in project docs.
---

# Matlab Helium Docs

## Goal

Keep project documentation aligned with the current code, especially:

- `README.md`
- `docs/usage_guide.md`
- top-level workflow descriptions
- module responsibility descriptions
- parameter and path rule descriptions

## Write From Code First

Treat code as the first source of truth.

Before editing docs:

1. Read the current entry script and affected modules.
2. Confirm the real process order, parameter names, and module boundaries.
3. Remove statements that no longer match the code.

Do not preserve outdated explanations just because they already exist in a document.

## Documentation Split

Use this default split unless the user asks otherwise:

- `README.md`
  - short overview
  - current structure
  - latest workflow summary
  - basic run method
- `docs/usage_guide.md`
  - detailed workflow
  - parameter section meanings
  - module responsibilities
  - documentation maintenance notes when useful

Keep README shorter than the detailed guide.

## Writing Rules

Write documentation in clear Chinese unless the user asks for another language.

Prefer:

- short sections
- stable headings
- flat lists
- direct wording
- code-aligned naming

Avoid:

- development history
- temporary debugging notes
- “old code / old entry” narration unless the user explicitly asks for migration history
- speculative behavior not confirmed in code
- repeating the same explanation in both README and the detailed guide

## Workflow Description Rules

When documenting the main pipeline:

1. Follow the actual order in `apps/run_batch_pipeline.m`.
2. Use the same stage names as the current code whenever practical.
3. Keep the stage order stable between code comments and docs.
4. If the entry script presents stages in parallel style, describe them in the same style.

If the code uses a single top-level entry with inline orchestration, describe it that way. Do not invent removed middle layers.

## Module Description Rules

For each formal module in `src/`, document only:

- what it does
- what inputs it expects at a high level
- what outputs it produces at a high level
- what stage of the pipeline it belongs to
- any important limits or usage notes

Do not over-document internal local helpers unless the user explicitly wants per-function detail.

## Parameter Description Rules

Describe parameters by editing frequency and responsibility:

- data directory and switches
- raw file rules
- output rules
- preprocess parameters
- RD parameters
- target-analysis and plotting parameters

If a parameter belongs to the entry script, document it there rather than attributing ownership to a lower module.

## What To Update

Update docs when any of the following changes:

- main process order
- stage names
- formal module set
- parameter names or locations
- result output rules
- path rules
- entry strategy

Small bug fixes that do not change behavior usually do not require doc edits.

## Output Standard

Aim for documents that feel tidy and intentional:

- concise but not skeletal
- structured but not bloated
- attractive through good hierarchy, not decoration

Prefer a calm, professional style over verbose explanation.
