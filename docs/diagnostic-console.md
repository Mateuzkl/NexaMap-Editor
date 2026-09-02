# Console de depuração do NexaMap

Referência estudada: [RME Redux](https://github.com/Open-Tibia-Tools/remeres-map-editor-redux/tree/0b5804e0d0b6ef6eedeeae952f9e42e6763fb3fa), clonado ao lado do NexaMap. O Redux usa o subsistema Console no CMake e registra exceções do loop de eventos. O NexaMap adota o mesmo subsistema também no projeto do Visual Studio, aproveitando seu próprio gravador de logs e os recursos de diagnóstico do wxWidgets já instalado.

## Como reproduzir o problema

1. Compile `vcproj/Editor.sln` em **Release x64**, com **v145**.
2. Abra `NexaMap-Debug.cmd` na raiz do NexaMap. Ele prioriza o executável gerado pela solução em `vcproj/x64/Release`, em vez de uma cópia antiga na raiz. O console mostra o caminho efetivamente usado e a data da compilação.
3. Selecione o cliente e o servidor como antes. O log registra o clique, a abertura do diálogo nativo, a pasta selecionada, a leitura de `config.lua`, a busca dos recursos e a atualização da tela.
4. Se ocorrer a falha, guarde o trecho final do console e o arquivo `nexamap.log` ao lado do executável. Se essa pasta não permitir gravação, o arquivo fica em `%LOCALAPPDATA%/NexaMap/logs/nexamap.log`; o caminho aparece no console.

O iniciador deixa o terminal aberto mesmo se o Windows encerrar o processo antes de chamar os tratadores de exceção. Ao abrir o executável diretamente, ele também abre um console e aguarda Enter nas falhas capturadas quando esse console pertence somente ao NexaMap. Encerramentos forçados podem fechar esse console; nesse caso use o iniciador.

Nas exceções fatais capturadas pelo wxWidgets no Windows, o editor tenta salvar `nexamap-crash-DATA-HORA-PID.dmp` na mesma pasta do log e registrar a pilha de chamadas. Mantenha o arquivo `NexaMap Editor.pdb` da mesma compilação junto do executável para que os endereços possam ser associados às funções e linhas. O código continua encerrando após uma falha fatal.

O log fica habilitado por padrão. A opção **Preferences > General > Write diagnostic log (nexamap.log)** controla a gravação em arquivo e exige reinício. O iniciador define `NEXAMAP_DIAGNOSTICS=1` para habilitar os logs mesmo quando uma configuração antiga os desativava.

Esta alteração instrumenta a falha; a causa do crash ao selecionar o servidor ainda depende do log da reprodução. A compilação e a execução ficam com o usuário.
