# Local inference documentation design

The shared guideline is maintained in
[miyako/local-inference-site-design](https://github.com/miyako/local-inference-site-design/blob/main/SITE-DESIGN.md).
This site adopts [version v1.0.0](https://github.com/miyako/local-inference-site-design/blob/v1.0.0/SITE-DESIGN.md).

This is a documentation reference, not a submodule or build dependency;
guideline updates are adopted explicitly alongside local implementation changes.

## Local implementation

- [Site styles](assets/css/style.scss)
- [Syntax highlighting](assets/css/code.css) and [copy controls](assets/css/copy.css)
- [Shared layout](_layouts/default.html) and [accessibility/clipboard behavior](_includes/footer-custom.html)
- [Home page and local stack](index.md)

## Local exceptions

None beyond the guideline's permitted engine-specific content and branding:
CTranslate2's burgundy accent, logo, CT2 model directories, and accurate
capability, endpoint, and platform information.

This reference remains excluded from generated Pages output in [_config.yml](_config.yml).
