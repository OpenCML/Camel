# An entry argument reaches both steps

This page is a fixture, not a reader-facing page: the program needs an entry argument, and the
command fence below gives it one. The compile step runs the pass the command names, so the argument
has to reach that step too -- a compile command built without it fails before the run is ever tried.

```cml
// greet.cml
func main(name: string) sync {
    println('hello ' + name)
}
```

```bash
camel greet.cml std::nvm --arg name=Camel
```

```text
hello Camel
```