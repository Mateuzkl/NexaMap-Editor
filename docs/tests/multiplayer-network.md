# Multiplayer: conexão e estabilidade

## Dois PCs

1. Use a mesma build do NexaMap, o mesmo client e as mesmas definições de itens nos dois PCs.
2. No PC A, abra uma cópia do mapa e escolha **Multiplayer > Host Session**.
3. Configure **Listening port: 49171**, **Maximum players: 2**, papel Editor e uma senha de teste. A contagem inclui o host.
4. Confirme. Na janela de multiplayer do host, confira a porta em escuta e copie um endereço LAN/VPN. Endereços de interfaces virtuais também podem aparecer: escolha a interface acessível pelo PC B.
5. No PC B, escolha **Join Session**, informe apenas o IP/hostname do PC A no campo Host/IP, a porta no campo Port e a mesma senha. Join usa uma aba vazia para proteger mapas existentes.
6. Confira `Connecting to IP:PORT`, `Authenticating`, `Synchronizing`, `Connected` no log. Aguarde o snapshot concluir antes de editar.
7. Altere um tile no A e outro no B. Confira os dois lados, revisões e undo/redo. Depois edite o mesmo tile simultaneamente: conflito deve ser rejeitado explicitamente.
8. Abra propriedades, mude IDs com aprovação do host, confirme o resultado e confira a liberação do lock ao terminar ou cancelar.
9. Feche uma aba cliente durante sincronização, reconecte e confira que recebe o mapa inteiro. Interrompa temporariamente a conexão de um cliente sincronizado e confira recuperação por journal/snapshot e manutenção do papel.
10. Feche o host; o cliente deve mostrar o motivo da desconexão. Repita fechar/reabrir a janela de participantes e fechar a aba com eventos pendentes.

**Mesmo PC:** `127.0.0.1`. Isso nunca identifica outro PC.
**LAN:** IPv4 da interface LAN do host. **VPN:** IP do host dentro da VPN.
**Internet direto:** endereço público do host, regra de firewall e redirecionamento TCP/NAT para a porta configurada. O editor não consulta serviços externos de IP nem configura o roteador.

O botão copia `IP:PORT`; os campos de Join continuam separados. A lista do formulário mostra os endereços candidatos; a janela de participantes mostra a porta após o bind bem-sucedido.

## Falhas esperadas

Teste senha incorreta, client diferente, definições de itens diferentes, terceira pessoa numa sessão limitada a dois, hostname inválido e porta sem host. Rejeições de protocolo devem chegar ao cliente com seu motivo, sem congelar a interface. A implementação aguarda por eventos por até dois segundos para concluir o frame parcialmente enviado e a mensagem de encerramento.

O wxWidgets nem sempre preserva o erro nativo do TCP até o evento de perda. Quando `SO_ERROR` está disponível, mostramos recusa/timeout específicos; caso contrário, a mensagem informa falha por recusa ou host inacessível, sem inventar uma causa exata. Falhas iniciais não ficam tentando conectar para sempre. Perda de uma conexão autenticada usa a política de reconexão existente.

No canvas, abra o menu sobre um teleport e altere/remova o tile pelo outro PC antes de escolher Go To Destination, Copy Destination, Rotate, Switch Door ou os comandos Copy ID/Name. Os comandos devem revalidar a seleção atual e retornar se ela deixou de ser válida. Repita com wall, carpet, table, creature e seleção vazia. Um destino válido deve navegar uma vez.

## Testes automatizados

Configure CMake com `BUILD_TESTING=ON` e `ENABLE_MULTIPLAYER_SESSION_TESTS=ON`, usando o mesmo toolchain e dependências do editor. Compile os alvos `multiplayer_protocol_tests` e `multiplayer_session_tests` e execute:

```text
ctest --test-dir <build> -C Release -R "^multiplayer_(protocol|session)_tests$" --output-on-failure
```

O alvo de sessão compila o código real do editor, Session, codec e ActionQueue, abre sockets wx reais em loopback e processa eventos/timers. A fixture substitui apenas o carregamento interativo de recursos e permite hospedar/entrar no mesmo processo. A configuração normal do editor continua permitindo apenas uma sessão multiplayer por processo. O teste não carrega configurações do usuário, não abre mapas reais e não mostra janelas.

O mesmo executável oferece `--startup-validation`, registrado no CTest como
`startup_validation_tests`. Essa execução separada cria palettes nativas ocultas,
verifica os pais dos controles em `wxStaticBoxSizer`, dimensões dos campos numéricos
e propagação de eventos. Também verifica catálogos independentes/recarregados,
duplicatas idênticas, nomes compartilhados por monster/NPC, conflitos reais e a
entrega única de mensagens de debug, preservando o logger normal de avisos.
Não substitui inspeção visual dos temas nem testes em diferentes monitores/DPI.

Os testes locais não comprovam firewall/NAT/VPN, edição prolongada em mapas reais, aparência/DPI nem interação manual dos menus. O roteiro em dois PCs continua necessário antes de declarar estabilidade nessas condições.

## Resultado local — 2026-09-05

Visual Studio 18 / v145, Release x64: compilação concluída com zero erros e zero avisos.
`multiplayer_session_tests` e `multiplayer_protocol_tests` passaram três vezes consecutivas (seis execuções, aproximadamente 34 segundos no total). A suíte de sessão executou:

- Challenge → Hello → Welcome → Snapshot → Ready → Connected em sockets wx reais, em loopback;
- alteração de tiles nos dois sentidos pelo codec e `ACTION_REMOTE`;
- reconexão preservando revisão e papel Viewer, sem recuperar indevidamente permissão de edição;
- entrega do motivo de senha incorreta, definições de itens diferentes, client diferente e identidade inválida;
- sessão cheia sem derrubar os participantes já conectados;
- porta fechada com mensagem de conexão recusada;
- perda durante snapshot de 6.000 tiles, reconexão com snapshot completo e resync pelo journal;
- destruição do cliente enquanto recebe snapshot;
- aquisição assíncrona de lock, liberação ao terminar e cancelamento ao destruir a janela;
- hostname inválido e resolução de `localhost` sem bloquear a GUI;
- limite da fila de envio durante uma transferência, sem continuar usando a transferência liberada;
- destruição da sessão antes de guards de metadados e lotes de ações.

Esses resultados usam mapas sintéticos em memória, sem carregar assets/configurações do usuário. Os callbacks do menu foram revisados e compilados, mas a interação visual com teleport/door/brush, assets reais e dois PCs continua seguindo o roteiro acima. Os testes não medem vazamentos com um profiler nem simulam todas as falhas possíveis de rede.

Após os ajustes de inicialização das palettes e do logger, o Release x64 voltou a
compilar com zero erros/avisos. As três suítes `multiplayer_session_tests`,
`startup_validation_tests` e `multiplayer_protocol_tests` passaram juntas
(11,12 s, 0,79 s e 0,40 s). A construção oculta das palettes não emitiu avisos de
parentesco em static boxes nem de largura insuficiente dos campos numéricos.

A auditoria confirmou seleção/tile/lista de itens sem validação nos comandos Go To Destination, Copy Destination, Rotate, Switch Door e Copy Server ID/Client ID/Name; dereferência de brush ausente nos seletores Wall/Carpet/Table; ponteiros de Item mantidos pelo seletor modal de borders; cópias de tiles sem dono nos caminhos de cancelamento/propriedades. Todos os callbacks do popup agora conferem o editor ativo, e comandos que alteram o mapa verificam permissão. Move To Tileset deixou de recolocar uma cópia antiga do tile após um diálogo que só altera configuração local.

Também foram corrigidos `SetOption` antes de `Connect`, falhas imediatas ignoradas, perda do motivo de rejeição, acesso à transferência depois de limpar a fila, timeout por subtração sem sinal de timestamps, destruição de sockets/timers em background, renovação de locks durante invalidação da coleção e callbacks de operações após destruir a sessão. Não houve mudança no formato do protocolo, HMAC, renderer, minimap ou caches gráficos.

## Arquivos desta correção

Novos:

- `source/multiplayer_transport.h` e `source/multiplayer_transport.cpp`;
- `tests/multiplayer_session_tests.cpp`;
- `docs/tests/multiplayer-network.md`.

Alterados:

- `source/multiplayer_session.h`, `source/multiplayer_session.cpp`, `source/multiplayer_window.cpp`;
- `source/map_display.h`, `source/map_display.cpp`, `source/main_menubar.cpp`;
- `source/action.h`, `source/action.cpp`, `source/map_tab.cpp`;
- `source/application.cpp`, `source/editor.h`: entradas de fixture disponíveis somente no alvo de testes;
- `CMakeLists.txt`, `source/CMakeLists.txt`, `vcproj/Project/Editor.vcxproj`, `vcproj/Project/Editor.vcxproj.filters`;
- `docs/multiplayer-design.md`.
