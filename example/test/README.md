This example shows how to define `[[test]]` targets and run them with `spn test`. It's a very simple feature. `spn` doesn't include a test framework, but it *does* allow you to define test binaries which:
- Have their own dependencies
- Are not compiled unless asked for (e.g. so consumers of a package don't pay for test compilation)
- Are executed in one command with `spn test`
