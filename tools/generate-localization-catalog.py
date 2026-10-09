#!/usr/bin/env python3
"""Build editable UTF-8 language packs from recovered retail strings.

The shipped client contained eleven complete translations. This tool reads
those Python assignment modules as data, writes JSON with real Unicode code
points (never ``\\u`` escapes), and fails if a supposed retail language is
missing or incomplete. Community-only locales remain small overlays and
inherit missing keys from English at runtime.
"""

from __future__ import annotations

import argparse
import ast
import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class LocaleSource:
    """Metadata and optional recovered source module for one locale."""

    native_name: str
    font_asset: str = ""
    source_module: str | None = None


# These source names match aoslib/strings/__init__.py in the recovered retail
# client. Russian, Polish and Turkish deliberately use Tuffy: retail selected
# it for those languages because the decorative Latin faces lack glyphs.
LOCALES: dict[str, LocaleSource] = {
    "en": LocaleSource("English", source_module="english.py"),
    "bg": LocaleSource("Български", "fonts/Tuffy_Bold.ttf"),
    "ru": LocaleSource("Русский", "fonts/Tuffy_Bold.ttf", "russian.py"),
    "uk": LocaleSource("Українська", "fonts/Tuffy_Bold.ttf"),
    "pl": LocaleSource("Polski", "fonts/Tuffy_Bold.ttf", "polish.py"),
    "cs": LocaleSource("Čeština", "fonts/Tuffy_Bold.ttf"),
    "de": LocaleSource("Deutsch", source_module="german.py"),
    "fr": LocaleSource("Français", source_module="french.py"),
    "es": LocaleSource("Español", source_module="spanish.py"),
    "es-MX": LocaleSource("Español (México)", source_module="spanish_mexico.py"),
    "pt-BR": LocaleSource("Português (Brasil)", source_module="portuguese_brazil.py"),
    "it": LocaleSource("Italiano", source_module="italian.py"),
    "tr": LocaleSource("Türkçe", "fonts/Tuffy_Bold.ttf", "turkish.py"),
    "ja": LocaleSource(
        "日本語", "fonts/NotoSansJP-SemiBold.ttf", "japanese.py"
    ),
}


COMMUNITY_OVERLAYS: dict[str, dict[str, str]] = {
    "bg": {
        "SETTINGS": "Настройки",
        "PLAYER_PROFILE": "Профил",
        "PLAY_ONLINE": "Избери мач",
        "JOIN_MATCH": "Присъедини се",
        "CREATE_MATCH": "Създай мач",
        "WELCOME": "Добре дошъл",
        "QUIT": "Изход",
        "BACK": "Назад",
        "SELECT": "Избери",
        "CANCEL": "Отказ",
        "DONE": "Готово",
        "DEFAULTS": "По подразбиране",
        "FRIENDS": "Приятели",
        "SERVER_BROWSER": "Списък със сървъри",
        "MATCH_LOBBY": "Лоби за мач",
        "MATCH_SETTINGS": "Настройки на мача",
        "START_GAME": "Стартирай играта",
        "CHANGE_TEAM": "Смени отбор",
        "CHANGE_CLASS": "Смени клас",
        "RESUME": "Продължи",
    },
    "uk": {
        "SETTINGS": "Налаштування",
        "PLAYER_PROFILE": "Профіль гравця",
        "PLAY_ONLINE": "Обрати матч",
        "JOIN_MATCH": "Приєднатися",
        "CREATE_MATCH": "Створити матч",
        "WELCOME": "Ласкаво просимо",
        "QUIT": "Вийти",
        "BACK": "Назад",
        "SELECT": "Обрати",
        "CANCEL": "Скасувати",
        "DONE": "Готово",
        "DEFAULTS": "Типові",
        "FRIENDS": "Друзі",
        "SERVER_BROWSER": "Список серверів",
        "MATCH_LOBBY": "Лобі матчу",
        "MATCH_SETTINGS": "Налаштування матчу",
        "START_GAME": "Почати гру",
        "CHANGE_TEAM": "Змінити команду",
        "CHANGE_CLASS": "Змінити клас",
        "RESUME": "Продовжити",
    },
    "cs": {
        "SETTINGS": "Nastavení",
        "PLAYER_PROFILE": "Profil hráče",
        "PLAY_ONLINE": "Vybrat zápas",
        "JOIN_MATCH": "Připojit se",
        "CREATE_MATCH": "Vytvořit zápas",
        "WELCOME": "Vítejte",
        "QUIT": "Ukončit",
        "BACK": "Zpět",
        "SELECT": "Vybrat",
        "CANCEL": "Zrušit",
        "DONE": "Hotovo",
        "DEFAULTS": "Výchozí",
        "FRIENDS": "Přátelé",
        "SERVER_BROWSER": "Seznam serverů",
        "MATCH_LOBBY": "Herní lobby",
        "MATCH_SETTINGS": "Nastavení zápasu",
        "START_GAME": "Spustit hru",
        "CHANGE_TEAM": "Změnit tým",
        "CHANGE_CLASS": "Změnit třídu",
        "RESUME": "Pokračovat",
    },
}


# These controls were added by the revival client and therefore cannot exist
# in the 2015 retail modules. Keep this overlay intentionally small and keyed
# by the exact TextDrawCommand identifiers used by the native frontend.
CLIENT_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "LANGUAGE": "Language",
        "LOGOUT": "Logout",
        "PLAYER IDENTITY": "Player Identity",
        "USERNAME": "Username",
        "PASSWORD": "Password",
        "SIGN IN": "Sign In",
        "REGISTER": "Register",
        "PLAY AS GUEST": "Play as Guest",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Sign in, register, or play as guest",
        "SAVE YOUR RECOVERY CODE": "Save Your Recovery Code",
        "REQUESTS": "Requests",
        "INVITES": "Invites",
        "SEARCH PLAYER...": "Search player...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Lobby Invitations",
        "CREATE LOBBY": "Create Lobby",
        "DECLINE": "Decline",
        "ACCEPT": "Accept",
        "FRIENDS + LOBBY": "Friends + Lobby",
    },
    "de": {
        "LANGUAGE": "Sprache",
        "LOGOUT": "Abmelden",
        "PLAYER IDENTITY": "Spieleridentität",
        "USERNAME": "Benutzername",
        "PASSWORD": "Passwort",
        "SIGN IN": "Anmelden",
        "REGISTER": "Registrieren",
        "PLAY AS GUEST": "Als Gast spielen",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Anmelden, registrieren oder als Gast spielen",
        "SAVE YOUR RECOVERY CODE": "Wiederherstellungscode speichern",
        "REQUESTS": "Anfragen",
        "INVITES": "Einladungen",
        "SEARCH PLAYER...": "Spieler suchen...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Lobby-Einladungen",
        "CREATE LOBBY": "Lobby erstellen",
        "DECLINE": "Ablehnen",
        "ACCEPT": "Annehmen",
        "FRIENDS + LOBBY": "Freunde + Lobby",
    },
    "bg": {
        "LANGUAGE": "Език",
        "LOGOUT": "Изход от профила",
        "PLAYER IDENTITY": "Профил на играча",
        "USERNAME": "Потребителско име",
        "PASSWORD": "Парола",
        "SIGN IN": "Вход",
        "REGISTER": "Регистрация",
        "PLAY AS GUEST": "Играй като гост",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Влез, регистрирай се или играй като гост",
        "SAVE YOUR RECOVERY CODE": "Запази кода си за възстановяване",
        "REQUESTS": "Заявки",
        "INVITES": "Покани",
        "SEARCH PLAYER...": "Търси играч...",
        "ONLINE": "На линия",
        "OFFLINE": "Извън линия",
        "LOBBY INVITATIONS": "Покани за лоби",
        "CREATE LOBBY": "Създай лоби",
        "DECLINE": "Откажи",
        "ACCEPT": "Приеми",
        "FRIENDS + LOBBY": "Приятели + Лоби",
    },
    "uk": {
        "LANGUAGE": "Мова",
        "LOGOUT": "Вийти з профілю",
        "PLAYER IDENTITY": "Обліковий запис гравця",
        "USERNAME": "Ім’я користувача",
        "PASSWORD": "Пароль",
        "SIGN IN": "Увійти",
        "REGISTER": "Зареєструватися",
        "PLAY AS GUEST": "Грати як гість",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Увійдіть, зареєструйтеся або грайте як гість",
        "SAVE YOUR RECOVERY CODE": "Збережіть код відновлення",
        "REQUESTS": "Запити",
        "INVITES": "Запрошення",
        "SEARCH PLAYER...": "Знайти гравця...",
        "ONLINE": "У мережі",
        "OFFLINE": "Не в мережі",
        "LOBBY INVITATIONS": "Запрошення до лобі",
        "CREATE LOBBY": "Створити лобі",
        "DECLINE": "Відхилити",
        "ACCEPT": "Прийняти",
        "FRIENDS + LOBBY": "Друзі + Лобі",
    },
    "cs": {
        "LANGUAGE": "Jazyk",
        "LOGOUT": "Odhlásit se",
        "PLAYER IDENTITY": "Účet hráče",
        "USERNAME": "Uživatelské jméno",
        "PASSWORD": "Heslo",
        "SIGN IN": "Přihlásit se",
        "REGISTER": "Registrovat",
        "PLAY AS GUEST": "Hrát jako host",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Přihlaste se, zaregistrujte se nebo hrajte jako host",
        "SAVE YOUR RECOVERY CODE": "Uložte si kód pro obnovení",
        "REQUESTS": "Žádosti",
        "INVITES": "Pozvánky",
        "SEARCH PLAYER...": "Hledat hráče...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Pozvánky do lobby",
        "CREATE LOBBY": "Vytvořit lobby",
        "DECLINE": "Odmítnout",
        "ACCEPT": "Přijmout",
        "FRIENDS + LOBBY": "Přátelé + Lobby",
    },
    "fr": {
        "LANGUAGE": "Langue",
        "LOGOUT": "Déconnexion",
        "PLAYER IDENTITY": "Identité du joueur",
        "USERNAME": "Nom d’utilisateur",
        "PASSWORD": "Mot de passe",
        "SIGN IN": "Connexion",
        "REGISTER": "Inscription",
        "PLAY AS GUEST": "Jouer en invité",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Connexion, inscription ou mode invité",
        "SAVE YOUR RECOVERY CODE": "Conservez votre code de récupération",
        "REQUESTS": "Demandes",
        "INVITES": "Invitations",
        "SEARCH PLAYER...": "Rechercher un joueur...",
        "ONLINE": "En ligne",
        "OFFLINE": "Hors ligne",
        "LOBBY INVITATIONS": "Invitations au salon",
        "CREATE LOBBY": "Créer un salon",
        "DECLINE": "Refuser",
        "ACCEPT": "Accepter",
        "FRIENDS + LOBBY": "Amis + Salon",
    },
    "es": {
        "LANGUAGE": "Idioma",
        "LOGOUT": "Cerrar sesión",
        "PLAYER IDENTITY": "Identidad del jugador",
        "USERNAME": "Nombre de usuario",
        "PASSWORD": "Contraseña",
        "SIGN IN": "Iniciar sesión",
        "REGISTER": "Registrarse",
        "PLAY AS GUEST": "Jugar como invitado",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Inicia sesión, regístrate o juega como invitado",
        "SAVE YOUR RECOVERY CODE": "Guarda tu código de recuperación",
        "REQUESTS": "Solicitudes",
        "INVITES": "Invitaciones",
        "SEARCH PLAYER...": "Buscar jugador...",
        "ONLINE": "En línea",
        "OFFLINE": "Desconectado",
        "LOBBY INVITATIONS": "Invitaciones a la sala",
        "CREATE LOBBY": "Crear sala",
        "DECLINE": "Rechazar",
        "ACCEPT": "Aceptar",
        "FRIENDS + LOBBY": "Amigos + Sala",
    },
    "ru": {
        "LANGUAGE": "Язык",
        "LOGOUT": "Выйти",
        "PLAYER IDENTITY": "Учётная запись игрока",
        "USERNAME": "Имя пользователя",
        "PASSWORD": "Пароль",
        "SIGN IN": "Войти",
        "REGISTER": "Регистрация",
        "PLAY AS GUEST": "Играть как гость",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Войдите, зарегистрируйтесь или играйте как гость",
        "SAVE YOUR RECOVERY CODE": "Сохраните код восстановления",
        "REQUESTS": "Запросы",
        "INVITES": "Приглашения",
        "SEARCH PLAYER...": "Найти игрока...",
        "ONLINE": "В сети",
        "OFFLINE": "Не в сети",
        "LOBBY INVITATIONS": "Приглашения в лобби",
        "CREATE LOBBY": "Создать лобби",
        "DECLINE": "Отклонить",
        "ACCEPT": "Принять",
        "FRIENDS + LOBBY": "Друзья + Лобби",
    },
    "ja": {
        "LANGUAGE": "言語",
        "LOGOUT": "ログアウト",
        "PLAYER IDENTITY": "プレイヤーID",
        "USERNAME": "ユーザー名",
        "PASSWORD": "パスワード",
        "SIGN IN": "ログイン",
        "REGISTER": "登録",
        "PLAY AS GUEST": "ゲストとしてプレイ",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "ログイン、登録、またはゲストでプレイ",
        "SAVE YOUR RECOVERY CODE": "復旧コードを保存してください",
        "REQUESTS": "リクエスト",
        "INVITES": "招待",
        "SEARCH PLAYER...": "プレイヤーを検索...",
        "ONLINE": "オンライン",
        "OFFLINE": "オフライン",
        "LOBBY INVITATIONS": "ロビーへの招待",
        "CREATE LOBBY": "ロビーを作成",
        "DECLINE": "辞退",
        "ACCEPT": "承認",
        "FRIENDS + LOBBY": "フレンド + ロビー",
    },
    "it": {
        "LANGUAGE": "Lingua",
        "LOGOUT": "Esci",
        "PLAYER IDENTITY": "Identità giocatore",
        "USERNAME": "Nome utente",
        "PASSWORD": "Password",
        "SIGN IN": "Accedi",
        "REGISTER": "Registrati",
        "PLAY AS GUEST": "Gioca come ospite",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Accedi, registrati o gioca come ospite",
        "SAVE YOUR RECOVERY CODE": "Salva il codice di recupero",
        "REQUESTS": "Richieste",
        "INVITES": "Inviti",
        "SEARCH PLAYER...": "Cerca giocatore...",
        "ONLINE": "In linea",
        "OFFLINE": "Non in linea",
        "LOBBY INVITATIONS": "Inviti alla lobby",
        "CREATE LOBBY": "Crea lobby",
        "DECLINE": "Rifiuta",
        "ACCEPT": "Accetta",
        "FRIENDS + LOBBY": "Amici + Lobby",
    },
    "pl": {
        "LANGUAGE": "Język",
        "LOGOUT": "Wyloguj",
        "PLAYER IDENTITY": "Konto gracza",
        "USERNAME": "Nazwa użytkownika",
        "PASSWORD": "Hasło",
        "SIGN IN": "Zaloguj się",
        "REGISTER": "Zarejestruj się",
        "PLAY AS GUEST": "Graj jako gość",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Zaloguj się, zarejestruj lub graj jako gość",
        "SAVE YOUR RECOVERY CODE": "Zapisz kod odzyskiwania",
        "REQUESTS": "Prośby",
        "INVITES": "Zaproszenia",
        "SEARCH PLAYER...": "Szukaj gracza...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Zaproszenia do lobby",
        "CREATE LOBBY": "Utwórz lobby",
        "DECLINE": "Odrzuć",
        "ACCEPT": "Akceptuj",
        "FRIENDS + LOBBY": "Znajomi + Lobby",
    },
    "pt-BR": {
        "LANGUAGE": "Idioma",
        "LOGOUT": "Sair",
        "PLAYER IDENTITY": "Identidade do jogador",
        "USERNAME": "Nome de usuário",
        "PASSWORD": "Senha",
        "SIGN IN": "Entrar",
        "REGISTER": "Registrar",
        "PLAY AS GUEST": "Jogar como convidado",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Entre, registre-se ou jogue como convidado",
        "SAVE YOUR RECOVERY CODE": "Salve seu código de recuperação",
        "REQUESTS": "Solicitações",
        "INVITES": "Convites",
        "SEARCH PLAYER...": "Buscar jogador...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Convites para lobby",
        "CREATE LOBBY": "Criar lobby",
        "DECLINE": "Recusar",
        "ACCEPT": "Aceitar",
        "FRIENDS + LOBBY": "Amigos + Lobby",
    },
    "tr": {
        "LANGUAGE": "Dil",
        "LOGOUT": "Çıkış",
        "PLAYER IDENTITY": "Oyuncu hesabı",
        "USERNAME": "Kullanıcı adı",
        "PASSWORD": "Şifre",
        "SIGN IN": "Giriş yap",
        "REGISTER": "Kayıt ol",
        "PLAY AS GUEST": "Misafir olarak oyna",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Giriş yap, kayıt ol veya misafir olarak oyna",
        "SAVE YOUR RECOVERY CODE": "Kurtarma kodunu kaydet",
        "REQUESTS": "İstekler",
        "INVITES": "Davetler",
        "SEARCH PLAYER...": "Oyuncu ara...",
        "ONLINE": "Çevrimiçi",
        "OFFLINE": "Çevrimdışı",
        "LOBBY INVITATIONS": "Lobi davetleri",
        "CREATE LOBBY": "Lobi oluştur",
        "DECLINE": "Reddet",
        "ACCEPT": "Kabul et",
        "FRIENDS + LOBBY": "Arkadaşlar + Lobi",
    },
}


# High-traffic native-client screens which did not exist in the retail Python
# frontend.  These are intentionally complete for the first major revival
# languages instead of leaving a half-translated friends/lobby/direct-connect
# flow around otherwise complete retail translations.
MAJOR_CLIENT_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ADD FRIEND": "Add Friend",
        "CREATE + INVITE": "Create + Invite",
        "STARTING MATCH...": "Starting match...",
        "WAITING FOR HOST...": "Waiting for host...",
        "JOINING MATCH...": "Joining match...",
        "RECONNECTING TO AOSPLAY...": "Reconnecting to AoSPlay...",
        "ACTION NEEDS ATTENTION": "Action Needs Attention",
        "LOBBY_DETAILS_UNAVAILABLE": "Lobby details unavailable",
        "MAP_PREVIEW_UNAVAILABLE": "Map preview unavailable",
        "NO_LOCAL_UGC_MAPS": "No local maps available",
        "SERVER_LOCATION": "Server location",
        "IP:PORT OR HOSTNAME": "IP:port or hostname",
        "HOST": "Host",
        "IN_GAME": "In game",
    },
    "bg": {
        "ADD FRIEND": "Добави приятел",
        "CREATE + INVITE": "Създай + покани",
        "STARTING MATCH...": "Стартиране на мача...",
        "WAITING FOR HOST...": "Изчакване на хоста...",
        "JOINING MATCH...": "Присъединяване към мача...",
        "RECONNECTING TO AOSPLAY...": "Повторно свързване с AoSPlay...",
        "ACTION NEEDS ATTENTION": "Необходимо е действие",
        "LOBBY_DETAILS_UNAVAILABLE": "Данните за лобито не са достъпни",
        "MAP_PREVIEW_UNAVAILABLE": "Прегледът на картата не е достъпен",
        "NO_LOCAL_UGC_MAPS": "Няма локални карти",
        "SERVER_LOCATION": "Адрес на сървъра",
        "IP:PORT OR HOSTNAME": "IP:порт или име на хост",
        "HOST": "Хост",
        "IN_GAME": "В играта",
    },
    "de": {
        "ADD FRIEND": "Freund hinzufügen",
        "CREATE + INVITE": "Erstellen + einladen",
        "STARTING MATCH...": "Spiel wird gestartet...",
        "WAITING FOR HOST...": "Auf Host warten...",
        "JOINING MATCH...": "Spiel wird beigetreten...",
        "RECONNECTING TO AOSPLAY...": "Verbindung zu AoSPlay wird wiederhergestellt...",
        "ACTION NEEDS ATTENTION": "Aktion erforderlich",
        "LOBBY_DETAILS_UNAVAILABLE": "Lobby-Details nicht verfügbar",
        "MAP_PREVIEW_UNAVAILABLE": "Kartenvorschau nicht verfügbar",
        "NO_LOCAL_UGC_MAPS": "Keine lokalen Karten verfügbar",
        "SERVER_LOCATION": "Serveradresse",
        "IP:PORT OR HOSTNAME": "IP:Port oder Hostname",
        "HOST": "Host",
        "IN_GAME": "Im Spiel",
    },
    "es": {
        "ADD FRIEND": "Añadir amigo",
        "CREATE + INVITE": "Crear + invitar",
        "STARTING MATCH...": "Iniciando partida...",
        "WAITING FOR HOST...": "Esperando al anfitrión...",
        "JOINING MATCH...": "Uniéndose a la partida...",
        "RECONNECTING TO AOSPLAY...": "Reconectando a AoSPlay...",
        "ACTION NEEDS ATTENTION": "Acción requerida",
        "LOBBY_DETAILS_UNAVAILABLE": "Detalles de la sala no disponibles",
        "MAP_PREVIEW_UNAVAILABLE": "Vista previa del mapa no disponible",
        "NO_LOCAL_UGC_MAPS": "No hay mapas locales disponibles",
        "SERVER_LOCATION": "Dirección del servidor",
        "IP:PORT OR HOSTNAME": "IP:puerto o nombre de host",
        "HOST": "Anfitrión",
        "IN_GAME": "En partida",
    },
    "fr": {
        "ADD FRIEND": "Ajouter un ami",
        "CREATE + INVITE": "Créer + inviter",
        "STARTING MATCH...": "Démarrage de la partie...",
        "WAITING FOR HOST...": "En attente de l’hôte...",
        "JOINING MATCH...": "Connexion à la partie...",
        "RECONNECTING TO AOSPLAY...": "Reconnexion à AoSPlay...",
        "ACTION NEEDS ATTENTION": "Action requise",
        "LOBBY_DETAILS_UNAVAILABLE": "Détails du salon indisponibles",
        "MAP_PREVIEW_UNAVAILABLE": "Aperçu de la carte indisponible",
        "NO_LOCAL_UGC_MAPS": "Aucune carte locale disponible",
        "SERVER_LOCATION": "Adresse du serveur",
        "IP:PORT OR HOSTNAME": "IP:port ou nom d’hôte",
        "HOST": "Hôte",
        "IN_GAME": "En jeu",
    },
    "ru": {
        "ADD FRIEND": "Добавить друга",
        "CREATE + INVITE": "Создать + пригласить",
        "STARTING MATCH...": "Запуск матча...",
        "WAITING FOR HOST...": "Ожидание хоста...",
        "JOINING MATCH...": "Подключение к матчу...",
        "RECONNECTING TO AOSPLAY...": "Повторное подключение к AoSPlay...",
        "ACTION NEEDS ATTENTION": "Требуется действие",
        "LOBBY_DETAILS_UNAVAILABLE": "Данные лобби недоступны",
        "MAP_PREVIEW_UNAVAILABLE": "Предпросмотр карты недоступен",
        "NO_LOCAL_UGC_MAPS": "Локальные карты отсутствуют",
        "SERVER_LOCATION": "Адрес сервера",
        "IP:PORT OR HOSTNAME": "IP:порт или имя хоста",
        "HOST": "Хост",
        "IN_GAME": "В игре",
    },
    "ja": {
        "ADD FRIEND": "フレンドを追加",
        "CREATE + INVITE": "作成して招待",
        "STARTING MATCH...": "マッチを開始中...",
        "WAITING FOR HOST...": "ホストを待機中...",
        "JOINING MATCH...": "マッチに参加中...",
        "RECONNECTING TO AOSPLAY...": "AoSPlayに再接続中...",
        "ACTION NEEDS ATTENTION": "操作が必要です",
        "LOBBY_DETAILS_UNAVAILABLE": "ロビー情報を取得できません",
        "MAP_PREVIEW_UNAVAILABLE": "マッププレビューを表示できません",
        "NO_LOCAL_UGC_MAPS": "ローカルマップがありません",
        "SERVER_LOCATION": "サーバーアドレス",
        "IP:PORT OR HOSTNAME": "IP:ポートまたはホスト名",
        "HOST": "ホスト",
        "IN_GAME": "ゲーム中",
    },
}


# Native status and recovery screens added after the first localization pass.
# Keep this separate from the compact identity/friends overlay above: these
# strings are a release gate for the complete connection, matchmaking and UGC
# flow, and the regression suite checks every key in the initial major packs.
NATIVE_FLOW_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ASSET_PRELOAD_FAILED": "Asset preload failed",
        "COMPATIBILITY_SHADER": "Compatibility shader",
        "CONTENT_REQUIRED": "Content required",
        "ERROR_TIMEOUT": "Connection timed out",
        "KEEP_RESOLUTION_PROMPT": "Do you want to keep this resolution setting?",
        "LEADERBOARD_UNAVAILABLE": "Leaderboard unavailable",
        "MAP_CREATOR_LOBBY_INFO": "Lobby info",
        "MATCHMAKING_OFFLINE": "Matchmaking offline",
        "MATCHMAKING_READY_TO_REFRESH": "Ready to refresh",
        "MATCHMAKING_UNAVAILABLE": "Matchmaking unavailable",
        "NETWORK_UNAVAILABLE": "Network unavailable",
        "NO_LOCAL_MAPS": "No local maps found",
        "NO_MATCH_LOBBIES_FOUND": "No open matches found",
        "NO_UGC_LOBBIES_FOUND": "No available lobbies",
        "SEARCHING_FOR_CUSTOM_MATCH": "Searching for a custom match",
        "SEARCHING_FOR_MATCH_LOBBIES": "Searching for match lobbies",
        "SEARCHING_FOR_PUBLIC_MATCH": "Searching for a public match",
        "SEARCHING_FOR_UGC_LOBBIES": "Searching for map creator lobbies",
        "SELECT_A_LOCAL_MAP": "Select a local map",
        "SELECT_UGC_LOBBY": "Select a lobby",
        "SERVER_CONNECTION_FAILED": "Server connection failed",
        "SERVER_SEARCH_FAILED": "Server search failed",
        "UGC_DELETING_MAP_MESSAGE": "Deleting local map",
        "UGC_DESCRIPTION": "Create and test a custom voxel map.",
        "UGC_OPERATION_ERROR": "The operation could not be completed",
    },
    "bg": {
        "ASSET_PRELOAD_FAILED": "Зареждането на ресурсите се провали",
        "COMPATIBILITY_SHADER": "Съвместим шейдър",
        "CONTENT_REQUIRED": "Необходимо е съдържание",
        "ERROR_TIMEOUT": "Времето за свързване изтече",
        "KEEP_RESOLUTION_PROMPT": "Искате ли да запазите тази резолюция?",
        "LEADERBOARD_UNAVAILABLE": "Класацията не е достъпна",
        "MAP_CREATOR_LOBBY_INFO": "Информация за лобито",
        "MATCHMAKING_OFFLINE": "Системата за мачове е офлайн",
        "MATCHMAKING_READY_TO_REFRESH": "Готово за обновяване",
        "MATCHMAKING_UNAVAILABLE": "Системата за мачове не е достъпна",
        "NETWORK_UNAVAILABLE": "Мрежата не е достъпна",
        "NO_LOCAL_MAPS": "Няма локални карти",
        "NO_MATCH_LOBBIES_FOUND": "Няма отворени мачове",
        "NO_UGC_LOBBIES_FOUND": "Няма достъпни лобита",
        "SEARCHING_FOR_CUSTOM_MATCH": "Търсене на персонализиран мач",
        "SEARCHING_FOR_MATCH_LOBBIES": "Търсене на лобита за мач",
        "SEARCHING_FOR_PUBLIC_MATCH": "Търсене на публичен мач",
        "SEARCHING_FOR_UGC_LOBBIES": "Търсене на лобита за създаване на карти",
        "SELECT_A_LOCAL_MAP": "Изберете локална карта",
        "SELECT_UGC_LOBBY": "Изберете лоби",
        "SERVER_CONNECTION_FAILED": "Свързването със сървъра се провали",
        "SERVER_SEARCH_FAILED": "Търсенето на сървъри се провали",
        "UGC_DELETING_MAP_MESSAGE": "Изтриване на локалната карта",
        "UGC_DESCRIPTION": "Създайте и тествайте собствена вокселна карта.",
        "UGC_OPERATION_ERROR": "Операцията не можа да бъде завършена",
    },
    "de": {
        "ASSET_PRELOAD_FAILED": "Vorladen der Ressourcen fehlgeschlagen",
        "COMPATIBILITY_SHADER": "Kompatibilitäts-Shader",
        "CONTENT_REQUIRED": "Inhalt erforderlich",
        "ERROR_TIMEOUT": "Zeitüberschreitung bei der Verbindung",
        "KEEP_RESOLUTION_PROMPT": "Möchtest du diese Auflösung beibehalten?",
        "LEADERBOARD_UNAVAILABLE": "Bestenliste nicht verfügbar",
        "MAP_CREATOR_LOBBY_INFO": "Lobby-Informationen",
        "MATCHMAKING_OFFLINE": "Spielsuche ist offline",
        "MATCHMAKING_READY_TO_REFRESH": "Bereit zum Aktualisieren",
        "MATCHMAKING_UNAVAILABLE": "Spielsuche nicht verfügbar",
        "NETWORK_UNAVAILABLE": "Netzwerk nicht verfügbar",
        "NO_LOCAL_MAPS": "Keine lokalen Karten gefunden",
        "NO_MATCH_LOBBIES_FOUND": "Keine offenen Spiele gefunden",
        "NO_UGC_LOBBIES_FOUND": "Keine verfügbaren Lobbys",
        "SEARCHING_FOR_CUSTOM_MATCH": "Benutzerdefiniertes Spiel wird gesucht",
        "SEARCHING_FOR_MATCH_LOBBIES": "Spiel-Lobbys werden gesucht",
        "SEARCHING_FOR_PUBLIC_MATCH": "Öffentliches Spiel wird gesucht",
        "SEARCHING_FOR_UGC_LOBBIES": "Karteneditor-Lobbys werden gesucht",
        "SELECT_A_LOCAL_MAP": "Lokale Karte auswählen",
        "SELECT_UGC_LOBBY": "Lobby auswählen",
        "SERVER_CONNECTION_FAILED": "Serververbindung fehlgeschlagen",
        "SERVER_SEARCH_FAILED": "Serversuche fehlgeschlagen",
        "UGC_DELETING_MAP_MESSAGE": "Lokale Karte wird gelöscht",
        "UGC_DESCRIPTION": "Erstelle und teste eine eigene Voxelkarte.",
        "UGC_OPERATION_ERROR": "Der Vorgang konnte nicht abgeschlossen werden",
    },
    "es": {
        "ASSET_PRELOAD_FAILED": "Error al precargar los recursos",
        "COMPATIBILITY_SHADER": "Sombreador de compatibilidad",
        "CONTENT_REQUIRED": "Contenido necesario",
        "ERROR_TIMEOUT": "Se agotó el tiempo de conexión",
        "KEEP_RESOLUTION_PROMPT": "¿Quieres mantener esta resolución?",
        "LEADERBOARD_UNAVAILABLE": "Clasificación no disponible",
        "MAP_CREATOR_LOBBY_INFO": "Información de la sala",
        "MATCHMAKING_OFFLINE": "El emparejamiento está desconectado",
        "MATCHMAKING_READY_TO_REFRESH": "Listo para actualizar",
        "MATCHMAKING_UNAVAILABLE": "Emparejamiento no disponible",
        "NETWORK_UNAVAILABLE": "Red no disponible",
        "NO_LOCAL_MAPS": "No se encontraron mapas locales",
        "NO_MATCH_LOBBIES_FOUND": "No se encontraron partidas abiertas",
        "NO_UGC_LOBBIES_FOUND": "No hay salas disponibles",
        "SEARCHING_FOR_CUSTOM_MATCH": "Buscando una partida personalizada",
        "SEARCHING_FOR_MATCH_LOBBIES": "Buscando salas de partida",
        "SEARCHING_FOR_PUBLIC_MATCH": "Buscando una partida pública",
        "SEARCHING_FOR_UGC_LOBBIES": "Buscando salas del creador de mapas",
        "SELECT_A_LOCAL_MAP": "Selecciona un mapa local",
        "SELECT_UGC_LOBBY": "Selecciona una sala",
        "SERVER_CONNECTION_FAILED": "Error de conexión con el servidor",
        "SERVER_SEARCH_FAILED": "Error al buscar servidores",
        "UGC_DELETING_MAP_MESSAGE": "Eliminando el mapa local",
        "UGC_DESCRIPTION": "Crea y prueba un mapa de vóxeles personalizado.",
        "UGC_OPERATION_ERROR": "No se pudo completar la operación",
    },
    "fr": {
        "ASSET_PRELOAD_FAILED": "Échec du préchargement des ressources",
        "COMPATIBILITY_SHADER": "Shader de compatibilité",
        "CONTENT_REQUIRED": "Contenu requis",
        "ERROR_TIMEOUT": "Délai de connexion dépassé",
        "KEEP_RESOLUTION_PROMPT": "Voulez-vous conserver cette résolution ?",
        "LEADERBOARD_UNAVAILABLE": "Classement indisponible",
        "MAP_CREATOR_LOBBY_INFO": "Informations du salon",
        "MATCHMAKING_OFFLINE": "Le matchmaking est hors ligne",
        "MATCHMAKING_READY_TO_REFRESH": "Prêt à actualiser",
        "MATCHMAKING_UNAVAILABLE": "Matchmaking indisponible",
        "NETWORK_UNAVAILABLE": "Réseau indisponible",
        "NO_LOCAL_MAPS": "Aucune carte locale trouvée",
        "NO_MATCH_LOBBIES_FOUND": "Aucune partie ouverte trouvée",
        "NO_UGC_LOBBIES_FOUND": "Aucun salon disponible",
        "SEARCHING_FOR_CUSTOM_MATCH": "Recherche d’une partie personnalisée",
        "SEARCHING_FOR_MATCH_LOBBIES": "Recherche de salons de partie",
        "SEARCHING_FOR_PUBLIC_MATCH": "Recherche d’une partie publique",
        "SEARCHING_FOR_UGC_LOBBIES": "Recherche de salons de création de cartes",
        "SELECT_A_LOCAL_MAP": "Sélectionnez une carte locale",
        "SELECT_UGC_LOBBY": "Sélectionnez un salon",
        "SERVER_CONNECTION_FAILED": "Échec de la connexion au serveur",
        "SERVER_SEARCH_FAILED": "Échec de la recherche de serveurs",
        "UGC_DELETING_MAP_MESSAGE": "Suppression de la carte locale",
        "UGC_DESCRIPTION": "Créez et testez une carte de voxels personnalisée.",
        "UGC_OPERATION_ERROR": "L’opération n’a pas pu être terminée",
    },
    "ru": {
        "ASSET_PRELOAD_FAILED": "Не удалось загрузить ресурсы",
        "COMPATIBILITY_SHADER": "Шейдер совместимости",
        "CONTENT_REQUIRED": "Требуется содержимое",
        "ERROR_TIMEOUT": "Время ожидания подключения истекло",
        "KEEP_RESOLUTION_PROMPT": "Сохранить это разрешение экрана?",
        "LEADERBOARD_UNAVAILABLE": "Таблица лидеров недоступна",
        "MAP_CREATOR_LOBBY_INFO": "Информация о лобби",
        "MATCHMAKING_OFFLINE": "Подбор матчей отключён",
        "MATCHMAKING_READY_TO_REFRESH": "Готово к обновлению",
        "MATCHMAKING_UNAVAILABLE": "Подбор матчей недоступен",
        "NETWORK_UNAVAILABLE": "Сеть недоступна",
        "NO_LOCAL_MAPS": "Локальные карты не найдены",
        "NO_MATCH_LOBBIES_FOUND": "Открытые матчи не найдены",
        "NO_UGC_LOBBIES_FOUND": "Доступных лобби нет",
        "SEARCHING_FOR_CUSTOM_MATCH": "Поиск пользовательского матча",
        "SEARCHING_FOR_MATCH_LOBBIES": "Поиск лобби матчей",
        "SEARCHING_FOR_PUBLIC_MATCH": "Поиск публичного матча",
        "SEARCHING_FOR_UGC_LOBBIES": "Поиск лобби редактора карт",
        "SELECT_A_LOCAL_MAP": "Выберите локальную карту",
        "SELECT_UGC_LOBBY": "Выберите лобби",
        "SERVER_CONNECTION_FAILED": "Не удалось подключиться к серверу",
        "SERVER_SEARCH_FAILED": "Не удалось найти серверы",
        "UGC_DELETING_MAP_MESSAGE": "Удаление локальной карты",
        "UGC_DESCRIPTION": "Создайте и испытайте собственную воксельную карту.",
        "UGC_OPERATION_ERROR": "Не удалось завершить операцию",
    },
    "ja": {
        "ASSET_PRELOAD_FAILED": "アセットの事前読み込みに失敗しました",
        "COMPATIBILITY_SHADER": "互換シェーダー",
        "CONTENT_REQUIRED": "コンテンツが必要です",
        "ERROR_TIMEOUT": "接続がタイムアウトしました",
        "KEEP_RESOLUTION_PROMPT": "この解像度を維持しますか？",
        "LEADERBOARD_UNAVAILABLE": "ランキングを利用できません",
        "MAP_CREATOR_LOBBY_INFO": "ロビー情報",
        "MATCHMAKING_OFFLINE": "マッチメイキングはオフラインです",
        "MATCHMAKING_READY_TO_REFRESH": "更新できます",
        "MATCHMAKING_UNAVAILABLE": "マッチメイキングを利用できません",
        "NETWORK_UNAVAILABLE": "ネットワークを利用できません",
        "NO_LOCAL_MAPS": "ローカルマップが見つかりません",
        "NO_MATCH_LOBBIES_FOUND": "参加可能なマッチが見つかりません",
        "NO_UGC_LOBBIES_FOUND": "参加可能なロビーがありません",
        "SEARCHING_FOR_CUSTOM_MATCH": "カスタムマッチを検索中",
        "SEARCHING_FOR_MATCH_LOBBIES": "マッチロビーを検索中",
        "SEARCHING_FOR_PUBLIC_MATCH": "公開マッチを検索中",
        "SEARCHING_FOR_UGC_LOBBIES": "マップ作成ロビーを検索中",
        "SELECT_A_LOCAL_MAP": "ローカルマップを選択",
        "SELECT_UGC_LOBBY": "ロビーを選択",
        "SERVER_CONNECTION_FAILED": "サーバーへの接続に失敗しました",
        "SERVER_SEARCH_FAILED": "サーバーの検索に失敗しました",
        "UGC_DELETING_MAP_MESSAGE": "ローカルマップを削除中",
        "UGC_DESCRIPTION": "カスタムボクセルマップを作成してテストします。",
        "UGC_OPERATION_ERROR": "操作を完了できませんでした",
    },
}


# These settings were introduced after the retail catalog. Ship them in every
# supported locale and retain them when rebuilding packs from retail sources.
APPEARANCE_SETTINGS_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "SHOW_SKINS": "Show skins",
        "SHOW_OTHER_SKINS": "Show other players' skins",
        "WEAPON_MOTION": "Weapon movement",
    },
    "bg": {
        "SHOW_SKINS": "Показване на скинове",
        "SHOW_OTHER_SKINS": "Показване на скиновете на другите играчи",
        "WEAPON_MOTION": "Движение на оръжието",
    },
    "ru": {
        "SHOW_SKINS": "Показывать скины",
        "SHOW_OTHER_SKINS": "Показывать скины других игроков",
        "WEAPON_MOTION": "Движение оружия",
    },
    "uk": {
        "SHOW_SKINS": "Показувати скіни",
        "SHOW_OTHER_SKINS": "Показувати скіни інших гравців",
        "WEAPON_MOTION": "Рух зброї",
    },
    "pl": {
        "SHOW_SKINS": "Pokaż skórki",
        "SHOW_OTHER_SKINS": "Pokaż skórki innych graczy",
        "WEAPON_MOTION": "Ruch broni",
    },
    "cs": {
        "SHOW_SKINS": "Zobrazit skiny",
        "SHOW_OTHER_SKINS": "Zobrazit skiny ostatních hráčů",
        "WEAPON_MOTION": "Pohyb zbraně",
    },
    "de": {
        "SHOW_SKINS": "Skins anzeigen",
        "SHOW_OTHER_SKINS": "Skins anderer Spieler anzeigen",
        "WEAPON_MOTION": "Waffenbewegung",
    },
    "fr": {
        "SHOW_SKINS": "Afficher les apparences",
        "SHOW_OTHER_SKINS": "Afficher les apparences des autres joueurs",
        "WEAPON_MOTION": "Mouvement de l'arme",
    },
    "es": {
        "SHOW_SKINS": "Mostrar aspectos",
        "SHOW_OTHER_SKINS": "Mostrar aspectos de otros jugadores",
        "WEAPON_MOTION": "Movimiento del arma",
    },
    "es-MX": {
        "SHOW_SKINS": "Mostrar aspectos",
        "SHOW_OTHER_SKINS": "Mostrar aspectos de otros jugadores",
        "WEAPON_MOTION": "Movimiento del arma",
    },
    "pt-BR": {
        "SHOW_SKINS": "Mostrar visuais",
        "SHOW_OTHER_SKINS": "Mostrar visuais de outros jogadores",
        "WEAPON_MOTION": "Movimento da arma",
    },
    "it": {
        "SHOW_SKINS": "Mostra aspetti",
        "SHOW_OTHER_SKINS": "Mostra aspetti degli altri giocatori",
        "WEAPON_MOTION": "Movimento dell'arma",
    },
    "tr": {
        "SHOW_SKINS": "Görünümleri göster",
        "SHOW_OTHER_SKINS": "Diğer oyuncuların görünümlerini göster",
        "WEAPON_MOTION": "Silah hareketi",
    },
    "ja": {
        "SHOW_SKINS": "スキンを表示",
        "SHOW_OTHER_SKINS": "他のプレイヤーのスキンを表示",
        "WEAPON_MOTION": "武器の動き",
    },
}


# Native-only jetpack/parachute key hints (retail parity decision D4: an
# option, off by default). Retail had no such text, so ship every locale.
PARITY_HUD_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ABILITY_HINTS": "Ability key hints",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Parachute deployed",
        "ABILITY_HINT_PARACHUTE_PENDING": "Parachute opens on descent",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: deploy parachute in air",
        "ABILITY_HINT_JETPACK": "{0}: thrust",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: thrust; release + land to recharge",
    },
    "bg": {
        "ABILITY_HINTS": "Подсказки за умения",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашутът е отворен",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашутът се отваря при спускане",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: отвори парашута във въздуха",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; пусни и кацни за презареждане",
    },
    "ru": {
        "ABILITY_HINTS": "Подсказки способностей",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашют раскрыт",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашют раскроется при снижении",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: раскрыть парашют в воздухе",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; отпустите и приземлитесь для перезарядки",
    },
    "uk": {
        "ABILITY_HINTS": "Підказки вмінь",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашут розкрито",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашут розкриється під час зниження",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: розкрити парашут у повітрі",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; відпустіть і приземліться для перезарядки",
    },
    "pl": {
        "ABILITY_HINTS": "Podpowiedzi umiejętności",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Spadochron otwarty",
        "ABILITY_HINT_PARACHUTE_PENDING": "Spadochron otworzy się przy opadaniu",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: otwórz spadochron w powietrzu",
        "ABILITY_HINT_JETPACK": "{0}: ciąg",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: ciąg; puść i wyląduj, aby naładować",
    },
    "cs": {
        "ABILITY_HINTS": "Nápověda schopností",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Padák otevřen",
        "ABILITY_HINT_PARACHUTE_PENDING": "Padák se otevře při klesání",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: otevřít padák ve vzduchu",
        "ABILITY_HINT_JETPACK": "{0}: tah",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: tah; pusťte a přistaňte pro dobití",
    },
    "de": {
        "ABILITY_HINTS": "Fähigkeitshinweise",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Fallschirm geöffnet",
        "ABILITY_HINT_PARACHUTE_PENDING": "Fallschirm öffnet sich beim Sinken",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: Fallschirm in der Luft öffnen",
        "ABILITY_HINT_JETPACK": "{0}: Schub",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: Schub; loslassen und landen zum Aufladen",
    },
    "fr": {
        "ABILITY_HINTS": "Aide des capacités",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Parachute déployé",
        "ABILITY_HINT_PARACHUTE_PENDING": "Le parachute s'ouvre en descente",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0} : déployer le parachute en l'air",
        "ABILITY_HINT_JETPACK": "{0} : poussée",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0} : poussée ; relâchez et atterrissez pour recharger",
    },
    "es": {
        "ABILITY_HINTS": "Ayudas de habilidad",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracaídas desplegado",
        "ABILITY_HINT_PARACHUTE_PENDING": "El paracaídas se abre al descender",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir el paracaídas en el aire",
        "ABILITY_HINT_JETPACK": "{0}: empuje",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: empuje; suelta y aterriza para recargar",
    },
    "es-MX": {
        "ABILITY_HINTS": "Ayudas de habilidad",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracaídas desplegado",
        "ABILITY_HINT_PARACHUTE_PENDING": "El paracaídas se abre al descender",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir el paracaídas en el aire",
        "ABILITY_HINT_JETPACK": "{0}: empuje",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: empuje; suelta y aterriza para recargar",
    },
    "pt-BR": {
        "ABILITY_HINTS": "Dicas de habilidade",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paraquedas aberto",
        "ABILITY_HINT_PARACHUTE_PENDING": "O paraquedas abre na descida",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir o paraquedas no ar",
        "ABILITY_HINT_JETPACK": "{0}: impulso",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: impulso; solte e aterrisse para recarregar",
    },
    "it": {
        "ABILITY_HINTS": "Suggerimenti abilità",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracadute aperto",
        "ABILITY_HINT_PARACHUTE_PENDING": "Il paracadute si apre in discesa",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: apri il paracadute in aria",
        "ABILITY_HINT_JETPACK": "{0}: spinta",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: spinta; rilascia e atterra per ricaricare",
    },
    "tr": {
        "ABILITY_HINTS": "Yetenek ipuçları",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paraşüt açıldı",
        "ABILITY_HINT_PARACHUTE_PENDING": "Paraşüt inişte açılır",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: havada paraşütü aç",
        "ABILITY_HINT_JETPACK": "{0}: itki",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: itki; bırak ve şarj için yere in",
    },
    "ja": {
        "ABILITY_HINTS": "能力のヒント",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "パラシュート展開中",
        "ABILITY_HINT_PARACHUTE_PENDING": "降下中にパラシュートが開きます",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: 空中でパラシュートを開く",
        "ABILITY_HINT_JETPACK": "{0}: 推進",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: 推進（離して着地で回復）",
    },
}


# Joining a server: the password prompt, the Arena mode and a failed load.
# SERVER_PASSWORD_PROMPT is retail's own English hint (loadingMenu.py
# EditBoxControl empty_text); retail has no string id for it and none for the
# rest, so every locale ships them.
PRESENTATION_SETTINGS_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "FALLBACK_MUSIC": "Music on silent servers",
        "FALLBACK_MUSIC_DESCRIPTION": "Play retail music when the server provides none. Server music takes priority.",
        "RAGDOLL_CORPSES": "Ragdoll corpses",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simulate fallen characters in Classic modes. Applies to new deaths."
    },
    "bg": {
        "FALLBACK_MUSIC": "Музика на тихи сървъри",
        "FALLBACK_MUSIC_DESCRIPTION": "Пуска оригинална музика, когато сървърът не подава. Музиката от сървъра е с предимство.",
        "RAGDOLL_CORPSES": "Физика на телата",
        "RAGDOLL_CORPSES_DESCRIPTION": "Симулира падналите герои в класическите режими. Прилага се при следващи смъртни случаи."
    },
    "ru": {
        "FALLBACK_MUSIC": "Музыка на тихих серверах",
        "FALLBACK_MUSIC_DESCRIPTION": "Оригинальная музыка, если сервер не передаёт свою. Музыка сервера имеет приоритет.",
        "RAGDOLL_CORPSES": "Физика тел",
        "RAGDOLL_CORPSES_DESCRIPTION": "Физика погибших персонажей в классических режимах. Для новых смертей."
    },
    "uk": {
        "FALLBACK_MUSIC": "Музика на тихих серверах",
        "FALLBACK_MUSIC_DESCRIPTION": "Оригінальна музика, якщо сервер не передає свою. Музика сервера має пріоритет.",
        "RAGDOLL_CORPSES": "Фізика тіл",
        "RAGDOLL_CORPSES_DESCRIPTION": "Фізика загиблих персонажів у класичних режимах. Для нових смертей."
    },
    "pl": {
        "FALLBACK_MUSIC": "Muzyka na cichych serwerach",
        "FALLBACK_MUSIC_DESCRIPTION": "Oryginalna muzyka, gdy serwer nie odtwarza własnej. Muzyka serwera ma pierwszeństwo.",
        "RAGDOLL_CORPSES": "Fizyka zwłok",
        "RAGDOLL_CORPSES_DESCRIPTION": "Symulacja poległych postaci w trybach klasycznych. Dotyczy kolejnych zgonów."
    },
    "cs": {
        "FALLBACK_MUSIC": "Hudba na tichých serverech",
        "FALLBACK_MUSIC_DESCRIPTION": "Původní hudba, pokud server nepřehrává vlastní. Hudba serveru má přednost.",
        "RAGDOLL_CORPSES": "Fyzika těl",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simulace padlých postav v klasických režimech. Platí pro nová úmrtí."
    },
    "de": {
        "FALLBACK_MUSIC": "Musik auf stillen Servern",
        "FALLBACK_MUSIC_DESCRIPTION": "Originalmusik, wenn der Server keine vorgibt. Servermusik hat Vorrang.",
        "RAGDOLL_CORPSES": "Ragdoll-Leichen",
        "RAGDOLL_CORPSES_DESCRIPTION": "Physik für gefallene Figuren in klassischen Modi. Gilt für neue Todesfälle."
    },
    "fr": {
        "FALLBACK_MUSIC": "Musique sur serveurs silencieux",
        "FALLBACK_MUSIC_DESCRIPTION": "Musique originale si le serveur n’en propose pas. La musique du serveur est prioritaire.",
        "RAGDOLL_CORPSES": "Physique des corps",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simule les personnages morts en modes classiques. Pour les prochaines morts."
    },
    "es": {
        "FALLBACK_MUSIC": "Música en servidores silenciosos",
        "FALLBACK_MUSIC_DESCRIPTION": "Música original si el servidor no ofrece ninguna. La música del servidor tiene prioridad.",
        "RAGDOLL_CORPSES": "Física de cadáveres",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simula personajes caídos en modos clásicos. Se aplica a nuevas muertes."
    },
    "es-MX": {
        "FALLBACK_MUSIC": "Música en servidores silenciosos",
        "FALLBACK_MUSIC_DESCRIPTION": "Música original si el servidor no ofrece ninguna. La música del servidor tiene prioridad.",
        "RAGDOLL_CORPSES": "Física de cadáveres",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simula personajes caídos en modos clásicos. Se aplica a nuevas muertes."
    },
    "pt-BR": {
        "FALLBACK_MUSIC": "Música em servidores silenciosos",
        "FALLBACK_MUSIC_DESCRIPTION": "Música original quando o servidor não oferece nenhuma. A música do servidor tem prioridade.",
        "RAGDOLL_CORPSES": "Física dos corpos",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simula personagens mortos nos modos clássicos. Vale para novas mortes."
    },
    "it": {
        "FALLBACK_MUSIC": "Musica sui server silenziosi",
        "FALLBACK_MUSIC_DESCRIPTION": "Musica originale se il server non ne offre. La musica del server ha la precedenza.",
        "RAGDOLL_CORPSES": "Fisica dei cadaveri",
        "RAGDOLL_CORPSES_DESCRIPTION": "Simula i personaggi caduti nelle modalità classiche. Si applica alle nuove morti."
    },
    "tr": {
        "FALLBACK_MUSIC": "Sessiz sunucularda müzik",
        "FALLBACK_MUSIC_DESCRIPTION": "Sunucu müzik sunmadığında orijinal müziği çalar. Sunucu müziği önceliklidir.",
        "RAGDOLL_CORPSES": "Ceset fiziği",
        "RAGDOLL_CORPSES_DESCRIPTION": "Klasik modlarda ölen karakterleri simüle eder. Yeni ölümlere uygulanır."
    },
    "ja": {
        "FALLBACK_MUSIC": "無音のサーバーで音楽を再生",
        "FALLBACK_MUSIC_DESCRIPTION": "サーバーに音楽がない場合はオリジナルの音楽を再生。サーバーの音楽が優先されます。",
        "RAGDOLL_CORPSES": "ラグドール",
        "RAGDOLL_CORPSES_DESCRIPTION": "クラシックモードで倒れたキャラクターの物理演算を行います。次の死亡から適用。"
    }
}

DISCORD_SETTINGS_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "DISCORD_PRESENCE": "Discord activity",
        "DISCORD_PRESENCE_DESCRIPTION": "Show your server, map, mode and player count on Discord. Requires the Discord desktop app. Private server addresses are hidden.",
        "DISCORD_JOIN": "Discord joining",
        "DISCORD_JOIN_DESCRIPTION": "Allow Discord Join Game and share a join button for public servers without a password. Private addresses and login credentials are never shared.",
    },
    "bg": {
        "DISCORD_PRESENCE": "Активност в Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Показва сървъра, картата, режима и броя играчи в Discord. Изисква настолното приложение Discord. Адресите на частни сървъри са скрити.",
        "DISCORD_JOIN": "Присъединяване чрез Discord",
        "DISCORD_JOIN_DESCRIPTION": "Позволява присъединяване към играта чрез Discord и споделя бутон за публични сървъри без парола. Частни адреси и данни за вход никога не се споделят.",
    },
    "de": {
        "DISCORD_PRESENCE": "Discord-Aktivität",
        "DISCORD_PRESENCE_DESCRIPTION": "Zeigt Server, Karte, Modus und Spielerzahl in Discord. Erfordert die Discord-Desktop-App. Private Serveradressen bleiben verborgen.",
        "DISCORD_JOIN": "Über Discord beitreten",
        "DISCORD_JOIN_DESCRIPTION": "Erlaubt den Spielbeitritt über Discord und teilt eine Beitrittsschaltfläche für öffentliche Server ohne Passwort. Private Adressen und Zugangsdaten werden niemals geteilt.",
    },
    "es": {
        "DISCORD_PRESENCE": "Actividad en Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Muestra el servidor, mapa, modo y número de jugadores en Discord. Requiere la aplicación de escritorio de Discord. Las direcciones de servidores privados se ocultan.",
        "DISCORD_JOIN": "Unirse desde Discord",
        "DISCORD_JOIN_DESCRIPTION": "Permite unirse a la partida desde Discord y compartir un botón para servidores públicos sin contraseña. Nunca se comparten direcciones privadas ni credenciales.",
    },
    "es-MX": {
        "DISCORD_PRESENCE": "Actividad en Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Muestra el servidor, mapa, modo y número de jugadores en Discord. Requiere la aplicación de escritorio de Discord. Las direcciones de servidores privados se ocultan.",
        "DISCORD_JOIN": "Unirse desde Discord",
        "DISCORD_JOIN_DESCRIPTION": "Permite unirse a la partida desde Discord y compartir un botón para servidores públicos sin contraseña. Nunca se comparten direcciones privadas ni credenciales.",
    },
    "fr": {
        "DISCORD_PRESENCE": "Activité Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Affiche le serveur, la carte, le mode et le nombre de joueurs sur Discord. Nécessite l'application de bureau Discord. Les adresses des serveurs privés sont masquées.",
        "DISCORD_JOIN": "Rejoindre via Discord",
        "DISCORD_JOIN_DESCRIPTION": "Autorise à rejoindre la partie via Discord et partage un bouton pour les serveurs publics sans mot de passe. Les adresses privées et les identifiants ne sont jamais partagés.",
    },
    "it": {
        "DISCORD_PRESENCE": "Attività su Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Mostra server, mappa, modalità e numero di giocatori su Discord. Richiede l'app desktop di Discord. Gli indirizzi dei server privati sono nascosti.",
        "DISCORD_JOIN": "Unisciti tramite Discord",
        "DISCORD_JOIN_DESCRIPTION": "Consente di unirsi alla partita tramite Discord e condivide un pulsante per i server pubblici senza password. Gli indirizzi privati e le credenziali non vengono mai condivisi.",
    },
    "pt-BR": {
        "DISCORD_PRESENCE": "Atividade no Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Mostra servidor, mapa, modo e número de jogadores no Discord. Requer o aplicativo para computador do Discord. Endereços de servidores privados ficam ocultos.",
        "DISCORD_JOIN": "Entrar pelo Discord",
        "DISCORD_JOIN_DESCRIPTION": "Permite entrar na partida pelo Discord e compartilha um botão para servidores públicos sem senha. Endereços privados e credenciais nunca são compartilhados.",
    },
    "ru": {
        "DISCORD_PRESENCE": "Активность в Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Показывает сервер, карту, режим и число игроков в Discord. Требуется настольное приложение Discord. Адреса частных серверов скрыты.",
        "DISCORD_JOIN": "Присоединение через Discord",
        "DISCORD_JOIN_DESCRIPTION": "Разрешает присоединяться к игре через Discord и показывает кнопку для общедоступных серверов без пароля. Частные адреса и данные для входа никогда не передаются.",
    },
    "uk": {
        "DISCORD_PRESENCE": "Активність у Discord",
        "DISCORD_PRESENCE_DESCRIPTION": "Показує сервер, карту, режим і кількість гравців у Discord. Потрібен настільний застосунок Discord. Адреси приватних серверів приховано.",
        "DISCORD_JOIN": "Приєднання через Discord",
        "DISCORD_JOIN_DESCRIPTION": "Дозволяє приєднуватися до гри через Discord і показує кнопку для публічних серверів без пароля. Приватні адреси та дані для входу ніколи не передаються.",
    },
    "pl": {
        "DISCORD_PRESENCE": "Aktywność na Discordzie",
        "DISCORD_PRESENCE_DESCRIPTION": "Pokazuje serwer, mapę, tryb i liczbę graczy na Discordzie. Wymaga aplikacji Discord na komputer. Adresy prywatnych serwerów są ukryte.",
        "DISCORD_JOIN": "Dołączanie przez Discord",
        "DISCORD_JOIN_DESCRIPTION": "Pozwala dołączać do gry przez Discord i udostępnia przycisk dla publicznych serwerów bez hasła. Prywatne adresy i dane logowania nigdy nie są udostępniane.",
    },
    "cs": {
        "DISCORD_PRESENCE": "Aktivita na Discordu",
        "DISCORD_PRESENCE_DESCRIPTION": "Zobrazuje server, mapu, režim a počet hráčů na Discordu. Vyžaduje aplikaci Discord pro počítač. Adresy soukromých serverů jsou skryté.",
        "DISCORD_JOIN": "Připojení přes Discord",
        "DISCORD_JOIN_DESCRIPTION": "Umožňuje připojení ke hře přes Discord a sdílí tlačítko pro veřejné servery bez hesla. Soukromé adresy a přihlašovací údaje se nikdy nesdílejí.",
    },
    "tr": {
        "DISCORD_PRESENCE": "Discord etkinliği",
        "DISCORD_PRESENCE_DESCRIPTION": "Sunucu, harita, mod ve oyuncu sayısını Discord'da gösterir. Discord masaüstü uygulaması gerekir. Özel sunucu adresleri gizlenir.",
        "DISCORD_JOIN": "Discord üzerinden katılma",
        "DISCORD_JOIN_DESCRIPTION": "Discord üzerinden oyuna katılmaya izin verir ve şifresiz herkese açık sunucular için katıl düğmesi paylaşır. Özel adresler ve giriş bilgileri asla paylaşılmaz.",
    },
    "ja": {
        "DISCORD_PRESENCE": "Discordのアクティビティ",
        "DISCORD_PRESENCE_DESCRIPTION": "サーバー、マップ、モード、プレイヤー数をDiscordに表示します。Discordのデスクトップアプリが必要です。非公開サーバーのアドレスは表示されません。",
        "DISCORD_JOIN": "Discordから参加",
        "DISCORD_JOIN_DESCRIPTION": "Discordからのゲーム参加を許可し、パスワードのない公開サーバーの参加ボタンを共有します。非公開のアドレスやログイン情報は共有されません。",
    },
}


BLOOD_SETTINGS_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "BLOOD_MARKS": "Lingering blood",
        "BLOOD_MARKS_DESCRIPTION": "Visual blood droplets and temporary stains on blocks in Classic and Standard. Lasts 20–32 seconds; does not change gameplay."
    },
    "bg": {
        "BLOOD_MARKS": "Следи от кръв",
        "BLOOD_MARKS_DESCRIPTION": "Визуални капки и временни петна по блоковете в Classic и Standard. Остават 20–32 секунди; не променят играта."
    },
    "cs": {
        "BLOOD_MARKS": "Krvavé stopy",
        "BLOOD_MARKS_DESCRIPTION": "Vizuální kapky krve a dočasné skvrny na blocích v Classic a Standard. Trvají 20–32 sekund a nemění hratelnost."
    },
    "de": {
        "BLOOD_MARKS": "Blutspuren",
        "BLOOD_MARKS_DESCRIPTION": "Sichtbare Blutstropfen und vorübergehende Flecken auf Blöcken in Classic und Standard. Bleiben 20–32 Sekunden; ohne Einfluss auf das Spiel."
    },
    "es": {
        "BLOOD_MARKS": "Rastros de sangre",
        "BLOOD_MARKS_DESCRIPTION": "Gotas de sangre y manchas temporales en los bloques en Classic y Standard. Duran 20–32 segundos; solo es un efecto visual."
    },
    "es-MX": {
        "BLOOD_MARKS": "Rastros de sangre",
        "BLOOD_MARKS_DESCRIPTION": "Gotas de sangre y manchas temporales en los bloques en Classic y Standard. Duran 20–32 segundos; solo es un efecto visual."
    },
    "fr": {
        "BLOOD_MARKS": "Traces de sang",
        "BLOOD_MARKS_DESCRIPTION": "Gouttes de sang et taches temporaires sur les blocs en Classic et Standard. Durent 20–32 secondes, sans effet sur le jeu."
    },
    "it": {
        "BLOOD_MARKS": "Tracce di sangue",
        "BLOOD_MARKS_DESCRIPTION": "Gocce di sangue e macchie temporanee sui blocchi in Classic e Standard. Durano 20–32 secondi; solo un effetto visivo."
    },
    "ja": {
        "BLOOD_MARKS": "血痕を残す",
        "BLOOD_MARKS_DESCRIPTION": "ClassicとStandardで血のしずくと一時的な血痕を表示します。20～32秒間残る視覚効果で、ゲームプレイには影響しません。"
    },
    "pl": {
        "BLOOD_MARKS": "Ślady krwi",
        "BLOOD_MARKS_DESCRIPTION": "Wizualne krople krwi i tymczasowe plamy na blokach w Classic i Standard. Pozostają przez 20–32 sekundy i nie wpływają na rozgrywkę."
    },
    "pt-BR": {
        "BLOOD_MARKS": "Marcas de sangue",
        "BLOOD_MARKS_DESCRIPTION": "Gotas de sangue e manchas temporárias nos blocos em Classic e Standard. Duram 20–32 segundos; apenas um efeito visual."
    },
    "ru": {
        "BLOOD_MARKS": "Следы крови",
        "BLOOD_MARKS_DESCRIPTION": "Капли крови и временные пятна на блоках в Classic и Standard. Остаются на 20–32 секунды и не влияют на игровой процесс."
    },
    "tr": {
        "BLOOD_MARKS": "Kan izleri",
        "BLOOD_MARKS_DESCRIPTION": "Classic ve Standard modlarında görsel kan damlaları ve bloklarda geçici lekeler. 20–32 saniye kalır; oynanışı etkilemez."
    },
    "uk": {
        "BLOOD_MARKS": "Сліди крові",
        "BLOOD_MARKS_DESCRIPTION": "Краплі крові й тимчасові плями на блоках у Classic та Standard. Залишаються на 20–32 секунди й не впливають на гру."
    }
}

SERVER_JOIN_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "SERVER_PASSWORD_PROMPT": "Type the server password and press enter to continue.",
        "SERVER_PASSWORD_WRONG": "Wrong password. Try again.",
        "SERVER_PASSWORD_REFUSED": "Wrong server password",
        "SERVER_PASSWORD_TIMEOUT": "No server password was entered in time",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Eliminate all enemies to win the round!",
        "LOAD_FAILED": "Loading failed",
    },
    "bg": {
        "SERVER_PASSWORD_PROMPT": "Въведете паролата на сървъра и натиснете Enter, за да продължите.",
        "SERVER_PASSWORD_WRONG": "Грешна парола. Опитайте отново.",
        "SERVER_PASSWORD_REFUSED": "Грешна парола за сървъра",
        "SERVER_PASSWORD_TIMEOUT": "Паролата на сървъра не беше въведена навреме",
        "ARENA": "Арена",
        "ARENA_DESCRIPTION": "Елиминирайте всички врагове, за да спечелите рунда!",
        "LOAD_FAILED": "Зареждането е неуспешно",
    },
    "ru": {
        "SERVER_PASSWORD_PROMPT": "Введите пароль сервера и нажмите Enter, чтобы продолжить.",
        "SERVER_PASSWORD_WRONG": "Неверный пароль. Попробуйте ещё раз.",
        "SERVER_PASSWORD_REFUSED": "Неверный пароль сервера",
        "SERVER_PASSWORD_TIMEOUT": "Пароль сервера не был введён вовремя",
        "ARENA": "Арена",
        "ARENA_DESCRIPTION": "Уничтожьте всех врагов, чтобы выиграть раунд!",
        "LOAD_FAILED": "Не удалось загрузить",
    },
    "uk": {
        "SERVER_PASSWORD_PROMPT": "Введіть пароль сервера та натисніть Enter, щоб продовжити.",
        "SERVER_PASSWORD_WRONG": "Неправильний пароль. Спробуйте ще раз.",
        "SERVER_PASSWORD_REFUSED": "Неправильний пароль сервера",
        "SERVER_PASSWORD_TIMEOUT": "Пароль сервера не було введено вчасно",
        "ARENA": "Арена",
        "ARENA_DESCRIPTION": "Знищте всіх ворогів, щоб виграти раунд!",
        "LOAD_FAILED": "Не вдалося завантажити",
    },
    "pl": {
        "SERVER_PASSWORD_PROMPT": "Wpisz hasło serwera i naciśnij Enter, aby kontynuować.",
        "SERVER_PASSWORD_WRONG": "Błędne hasło. Spróbuj ponownie.",
        "SERVER_PASSWORD_REFUSED": "Błędne hasło serwera",
        "SERVER_PASSWORD_TIMEOUT": "Nie podano hasła serwera na czas",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Wyeliminuj wszystkich wrogów, aby wygrać rundę!",
        "LOAD_FAILED": "Wczytywanie nie powiodło się",
    },
    "cs": {
        "SERVER_PASSWORD_PROMPT": "Zadejte heslo serveru a pokračujte stisknutím klávesy Enter.",
        "SERVER_PASSWORD_WRONG": "Nesprávné heslo. Zkuste to znovu.",
        "SERVER_PASSWORD_REFUSED": "Nesprávné heslo serveru",
        "SERVER_PASSWORD_TIMEOUT": "Heslo serveru nebylo zadáno včas",
        "ARENA": "Aréna",
        "ARENA_DESCRIPTION": "Zlikvidujte všechny nepřátele a vyhrajte kolo!",
        "LOAD_FAILED": "Načítání se nezdařilo",
    },
    "de": {
        "SERVER_PASSWORD_PROMPT": "Gib das Serverpasswort ein und drücke Enter, um fortzufahren.",
        "SERVER_PASSWORD_WRONG": "Falsches Passwort. Versuche es erneut.",
        "SERVER_PASSWORD_REFUSED": "Falsches Serverpasswort",
        "SERVER_PASSWORD_TIMEOUT": "Das Serverpasswort wurde nicht rechtzeitig eingegeben",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Schalte alle Gegner aus, um die Runde zu gewinnen!",
        "LOAD_FAILED": "Laden fehlgeschlagen",
    },
    "fr": {
        "SERVER_PASSWORD_PROMPT": "Saisissez le mot de passe du serveur et appuyez sur Entrée pour continuer.",
        "SERVER_PASSWORD_WRONG": "Mot de passe incorrect. Réessayez.",
        "SERVER_PASSWORD_REFUSED": "Mot de passe du serveur incorrect",
        "SERVER_PASSWORD_TIMEOUT": "Le mot de passe du serveur n'a pas été saisi à temps",
        "ARENA": "Arène",
        "ARENA_DESCRIPTION": "Éliminez tous les ennemis pour gagner la manche !",
        "LOAD_FAILED": "Échec du chargement",
    },
    "es": {
        "SERVER_PASSWORD_PROMPT": "Escribe la contraseña del servidor y pulsa Intro para continuar.",
        "SERVER_PASSWORD_WRONG": "Contraseña incorrecta. Inténtalo de nuevo.",
        "SERVER_PASSWORD_REFUSED": "Contraseña del servidor incorrecta",
        "SERVER_PASSWORD_TIMEOUT": "No se introdujo la contraseña del servidor a tiempo",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "¡Elimina a todos los enemigos para ganar la ronda!",
        "LOAD_FAILED": "Error al cargar",
    },
    "es-MX": {
        "SERVER_PASSWORD_PROMPT": "Escribe la contraseña del servidor y presiona Enter para continuar.",
        "SERVER_PASSWORD_WRONG": "Contraseña incorrecta. Intenta de nuevo.",
        "SERVER_PASSWORD_REFUSED": "Contraseña del servidor incorrecta",
        "SERVER_PASSWORD_TIMEOUT": "No se ingresó la contraseña del servidor a tiempo",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "¡Elimina a todos los enemigos para ganar la ronda!",
        "LOAD_FAILED": "Error al cargar",
    },
    "pt-BR": {
        "SERVER_PASSWORD_PROMPT": "Digite a senha do servidor e pressione Enter para continuar.",
        "SERVER_PASSWORD_WRONG": "Senha incorreta. Tente novamente.",
        "SERVER_PASSWORD_REFUSED": "Senha do servidor incorreta",
        "SERVER_PASSWORD_TIMEOUT": "A senha do servidor não foi digitada a tempo",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Elimine todos os inimigos para vencer a rodada!",
        "LOAD_FAILED": "Falha ao carregar",
    },
    "it": {
        "SERVER_PASSWORD_PROMPT": "Digita la password del server e premi Invio per continuare.",
        "SERVER_PASSWORD_WRONG": "Password errata. Riprova.",
        "SERVER_PASSWORD_REFUSED": "Password del server errata",
        "SERVER_PASSWORD_TIMEOUT": "La password del server non è stata inserita in tempo",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Elimina tutti i nemici per vincere il round!",
        "LOAD_FAILED": "Caricamento non riuscito",
    },
    "tr": {
        "SERVER_PASSWORD_PROMPT": "Sunucu şifresini yazın ve devam etmek için Enter'a basın.",
        "SERVER_PASSWORD_WRONG": "Yanlış şifre. Tekrar deneyin.",
        "SERVER_PASSWORD_REFUSED": "Sunucu şifresi yanlış",
        "SERVER_PASSWORD_TIMEOUT": "Sunucu şifresi zamanında girilmedi",
        "ARENA": "Arena",
        "ARENA_DESCRIPTION": "Raundu kazanmak için tüm düşmanları yok edin!",
        "LOAD_FAILED": "Yükleme başarısız",
    },
    "ja": {
        "SERVER_PASSWORD_PROMPT": "サーバーのパスワードを入力し、Enterキーを押して続行してください。",
        "SERVER_PASSWORD_WRONG": "パスワードが違います。もう一度入力してください。",
        "SERVER_PASSWORD_REFUSED": "サーバーのパスワードが違います",
        "SERVER_PASSWORD_TIMEOUT": "時間内にサーバーのパスワードが入力されませんでした",
        "ARENA": "アリーナ",
        "ARENA_DESCRIPTION": "敵を全滅させてラウンドに勝利しよう！",
        "LOAD_FAILED": "読み込みに失敗しました",
    },
}


# The shipped Japanese module accidentally mixed six Simplified-Chinese
# characters into sixteen labels. The retail Japanese fonts correctly omit
# those glyphs, so preserving the typos produces visible question marks.
# Apply narrow, language-correct repairs instead of silently substituting an
# unrelated system font.
JAPANESE_TEXT_REPLACEMENTS: dict[str, str] = {
    "弹": "弾",
    "訂阅": "購読",
    "尝試": "挑戦",
    "悬柱鏡": "吊り橋",
    "道标識": "道路標識",
    "村の体別仓": "村の納屋",
    "WW2体別仓": "WW2の納屋",
}

# One recovered Turkish label contains a literal question mark inside a word,
# which is the classic symptom of the original source having crossed a legacy
# code page. Keep the repair narrow and test it like the Japanese depot fixes.
TURKISH_TEXT_REPLACEMENTS: dict[str, str] = {
    "haz?r": "hazır",
}


def _replace_all(value: str, replacements: dict[str, str]) -> str:
    """Apply a small deterministic table of recovered-source repairs."""

    for source, replacement in replacements.items():
        value = value.replace(source, replacement)
    return value


def repair_recovered_japanese(strings: dict[str, str]) -> dict[str, str]:
    """Repair the six known Simplified-Chinese intrusions in retail Japanese."""

    repaired: dict[str, str] = {}
    for key, value in strings.items():
        for source, replacement in JAPANESE_TEXT_REPLACEMENTS.items():
            value = value.replace(source, replacement)
        repaired[key] = value
    return repaired


def validate_japanese_source(strings: dict[str, str]) -> None:
    """Reject the known broken depot whose Japanese table contains Russian.

    Several preserved client folders contain a damaged ``japanese.py`` with
    156 Russian values copied into it. Loading those strings is valid UTF-8,
    so an encoding-only test cannot detect the corruption. Generation must
    fail loudly instead of producing a readable but semantically wrong pack.

    Args:
        strings: Repaired Japanese source strings.

    Raises:
        ValueError: If any Cyrillic scalar remains in the Japanese table.
    """

    polluted = sorted(
        key
        for key, value in strings.items()
        if any("\u0400" <= character <= "\u04ff" for character in value)
    )
    if polluted:
        preview = ", ".join(polluted[:5])
        raise ValueError(
            "Japanese source is polluted with Cyrillic text; use the clean "
            f"retail strings directory ({len(polluted)} affected keys: {preview})"
        )


def recovered_strings(path: Path) -> dict[str, str]:
    """Extract constant-style string assignments from one recovered module.

    Args:
        path: UTF-8 Python source module containing retail string constants.

    Returns:
        A deterministic key-sorted mapping of localization identifiers.

    Raises:
        FileNotFoundError: If the selected recovered module is absent.
        SyntaxError: If the module is no longer parseable Python source.
        ValueError: If it contains no usable string assignments.
    """

    module = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    result: dict[str, str] = {}
    for statement in module.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        # Retail also keys score reasons and award names in mixed case
        # (TDM_Kill, TC_Contend, MOST_Kills); keep every constant-style name.
        if not isinstance(target, ast.Name) or not target.id[:1].isupper():
            continue
        try:
            value = ast.literal_eval(statement.value)
        except (ValueError, TypeError, SyntaxError):
            continue
        if isinstance(value, str):
            result[target.id] = value
    if not result:
        raise ValueError(f"no localization strings recovered from {path}")
    return dict(sorted(result.items()))


CLASSIC_APPEARANCE_OVERLAYS: dict[str, dict[str, str]] = {'en': {'INVENTORY': 'Inventory',
        'INVENTORY_DISCOVERY_HINT': 'Skins, levels & player stats',
        'CLASSIC_SKY': 'Classic sky',
        'CLASSIC_SKY_DEFAULT': 'Map default',
        'CLASSIC_SKY_RANDOM': 'Random (map colors)',
        'CLASSIC_SKY_CLEAR': 'Clear',
        'CLASSIC_SKY_OVERCAST': 'Overcast',
        'CLASSIC_SKY_DESERT': 'Desert',
        'CLASSIC_SKY_SUNSET': 'Sunset',
        'CLASSIC_SKY_NIGHT': 'Night',
        'CLASSIC_SKY_SNOW': 'Snow',
        'CLASSIC_SKY_DESCRIPTION': 'Sky for legacy VXL maps. Random picks a sky from the terrain '
                                   'colors once per map. Local appearance only.',
        'CLASSIC_FOG': 'Classic fog color',
        'CLASSIC_FOG_GRAY': 'Classic gray (default)',
        'CLASSIC_FOG_SERVER': 'Server / map',
        'CLASSIC_FOG_SKY': 'Match sky',
        'CLASSIC_FOG_CUSTOM': 'Custom RGB',
        'CLASSIC_FOG_RED': 'Fog red',
        'CLASSIC_FOG_GREEN': 'Fog green',
        'CLASSIC_FOG_BLUE': 'Fog blue',
        'CLASSIC_FOG_DESCRIPTION': 'Legacy VXL maps only: use server fog, match the sky, or mix '
                                   'red, green and blue below. Fog distance stays unchanged. '
                                   'Cancel restores your previous colors.'},
 'bg': {'INVENTORY': 'Инвентар',
        'INVENTORY_DISCOVERY_HINT': 'Скинове, нива и статистики',
        'CLASSIC_SKY': 'Класическо небе',
        'CLASSIC_SKY_DEFAULT': 'Според картата',
        'CLASSIC_SKY_RANDOM': 'Случайно (по цветове)',
        'CLASSIC_SKY_CLEAR': 'Ясно',
        'CLASSIC_SKY_OVERCAST': 'Облачно',
        'CLASSIC_SKY_DESERT': 'Пустиня',
        'CLASSIC_SKY_SUNSET': 'Залез',
        'CLASSIC_SKY_NIGHT': 'Нощ',
        'CLASSIC_SKY_SNOW': 'Сняг',
        'CLASSIC_SKY_DESCRIPTION': 'Небе за старите VXL карти. Случайният избор се съобразява с '
                                   'цветовете на терена веднъж на карта. Само локален изглед.',
        'CLASSIC_FOG': 'Цвят на мъглата',
        'CLASSIC_FOG_GRAY': 'Класическо сиво (по подразбиране)',
        'CLASSIC_FOG_SERVER': 'Сървър / карта',
        'CLASSIC_FOG_SKY': 'Според небето',
        'CLASSIC_FOG_CUSTOM': 'Собствен RGB',
        'CLASSIC_FOG_RED': 'Мъгла: червено',
        'CLASSIC_FOG_GREEN': 'Мъгла: зелено',
        'CLASSIC_FOG_BLUE': 'Мъгла: синьо',
        'CLASSIC_FOG_DESCRIPTION': 'За стари VXL карти: мъгла от сървъра, според небето или '
                                   'собствен RGB цвят. Дистанцията не се променя. Отказ връща '
                                   'предишните цветове.'},
 'de': {'INVENTORY': 'Inventar',
        'INVENTORY_DISCOVERY_HINT': 'Skins, Level & Statistiken',
        'CLASSIC_SKY': 'Classic-Himmel',
        'CLASSIC_SKY_DEFAULT': 'Kartenstandard',
        'CLASSIC_SKY_RANDOM': 'Zufall (Kartenfarben)',
        'CLASSIC_SKY_CLEAR': 'Klar',
        'CLASSIC_SKY_OVERCAST': 'Bewölkt',
        'CLASSIC_SKY_DESERT': 'Wüste',
        'CLASSIC_SKY_SUNSET': 'Sonnenuntergang',
        'CLASSIC_SKY_NIGHT': 'Nacht',
        'CLASSIC_SKY_SNOW': 'Schnee',
        'CLASSIC_SKY_DESCRIPTION': 'Himmel für alte VXL-Karten. Zufall wählt einmal pro Karte '
                                   'anhand der Geländefarben. Nur lokale Darstellung.',
        'CLASSIC_FOG': 'Classic-Nebelfarbe',
        'CLASSIC_FOG_GRAY': 'Klassisches Grau (Standard)',
        'CLASSIC_FOG_SERVER': 'Server / Karte',
        'CLASSIC_FOG_SKY': 'Passend zum Himmel',
        'CLASSIC_FOG_CUSTOM': 'Eigenes RGB',
        'CLASSIC_FOG_RED': 'Nebel: Rot',
        'CLASSIC_FOG_GREEN': 'Nebel: Grün',
        'CLASSIC_FOG_BLUE': 'Nebel: Blau',
        'CLASSIC_FOG_DESCRIPTION': 'Für alte VXL-Karten: Serverfarbe, Himmelfarbe oder eigenes '
                                   'RGB. Die Nebeldistanz bleibt gleich. Abbrechen stellt die '
                                   'vorherigen Farben wieder her.'},
 'fr': {'INVENTORY': 'Inventaire',
        'INVENTORY_DISCOVERY_HINT': 'Skins, niveaux et statistiques',
        'CLASSIC_SKY': 'Ciel Classic',
        'CLASSIC_SKY_DEFAULT': 'Ciel de la carte',
        'CLASSIC_SKY_RANDOM': 'Aléatoire (couleurs)',
        'CLASSIC_SKY_CLEAR': 'Dégagé',
        'CLASSIC_SKY_OVERCAST': 'Couvert',
        'CLASSIC_SKY_DESERT': 'Désert',
        'CLASSIC_SKY_SUNSET': 'Coucher de soleil',
        'CLASSIC_SKY_NIGHT': 'Nuit',
        'CLASSIC_SKY_SNOW': 'Neige',
        'CLASSIC_SKY_DESCRIPTION': 'Ciel des anciennes cartes VXL. Le choix aléatoire suit les '
                                   'couleurs du terrain une fois par carte. Apparence locale '
                                   'uniquement.',
        'CLASSIC_FOG': 'Couleur du brouillard',
        'CLASSIC_FOG_GRAY': 'Gris classique (par défaut)',
        'CLASSIC_FOG_SERVER': 'Serveur / carte',
        'CLASSIC_FOG_SKY': 'Selon le ciel',
        'CLASSIC_FOG_CUSTOM': 'RVB personnalisé',
        'CLASSIC_FOG_RED': 'Brouillard : rouge',
        'CLASSIC_FOG_GREEN': 'Brouillard : vert',
        'CLASSIC_FOG_BLUE': 'Brouillard : bleu',
        'CLASSIC_FOG_DESCRIPTION': 'Anciennes cartes VXL : couleur du serveur, du ciel ou RVB '
                                   'personnalisé. Distance inchangée. Annuler restaure les '
                                   'couleurs précédentes.'},
 'es': {'INVENTORY': 'Inventario',
        'INVENTORY_DISCOVERY_HINT': 'Skins, niveles y estadísticas',
        'CLASSIC_SKY': 'Cielo Classic',
        'CLASSIC_SKY_DEFAULT': 'Según el mapa',
        'CLASSIC_SKY_RANDOM': 'Aleatorio (colores)',
        'CLASSIC_SKY_CLEAR': 'Despejado',
        'CLASSIC_SKY_OVERCAST': 'Nublado',
        'CLASSIC_SKY_DESERT': 'Desierto',
        'CLASSIC_SKY_SUNSET': 'Atardecer',
        'CLASSIC_SKY_NIGHT': 'Noche',
        'CLASSIC_SKY_SNOW': 'Nieve',
        'CLASSIC_SKY_DESCRIPTION': 'Cielo de mapas VXL antiguos. Aleatorio elige según los colores '
                                   'del terreno una vez por mapa. Solo apariencia local.',
        'CLASSIC_FOG': 'Color de niebla Classic',
        'CLASSIC_FOG_GRAY': 'Gris clásico (predeterminado)',
        'CLASSIC_FOG_SERVER': 'Servidor / mapa',
        'CLASSIC_FOG_SKY': 'Según el cielo',
        'CLASSIC_FOG_CUSTOM': 'RGB personalizado',
        'CLASSIC_FOG_RED': 'Niebla: rojo',
        'CLASSIC_FOG_GREEN': 'Niebla: verde',
        'CLASSIC_FOG_BLUE': 'Niebla: azul',
        'CLASSIC_FOG_DESCRIPTION': 'Mapas VXL antiguos: color del servidor, del cielo o RGB '
                                   'propio. La distancia no cambia. Cancelar restaura los colores '
                                   'anteriores.'},
 'it': {'INVENTORY': 'Inventario',
        'INVENTORY_DISCOVERY_HINT': 'Skin, livelli e statistiche',
        'CLASSIC_SKY': 'Cielo Classic',
        'CLASSIC_SKY_DEFAULT': 'Predefinito mappa',
        'CLASSIC_SKY_RANDOM': 'Casuale (colori mappa)',
        'CLASSIC_SKY_CLEAR': 'Sereno',
        'CLASSIC_SKY_OVERCAST': 'Nuvoloso',
        'CLASSIC_SKY_DESERT': 'Deserto',
        'CLASSIC_SKY_SUNSET': 'Tramonto',
        'CLASSIC_SKY_NIGHT': 'Notte',
        'CLASSIC_SKY_SNOW': 'Neve',
        'CLASSIC_SKY_DESCRIPTION': 'Cielo delle vecchie mappe VXL. Casuale sceglie in base ai '
                                   'colori del terreno una volta per mappa. Solo aspetto locale.',
        'CLASSIC_FOG': 'Colore nebbia Classic',
        'CLASSIC_FOG_GRAY': 'Grigio classico (predefinito)',
        'CLASSIC_FOG_SERVER': 'Server / mappa',
        'CLASSIC_FOG_SKY': 'Come il cielo',
        'CLASSIC_FOG_CUSTOM': 'RGB personale',
        'CLASSIC_FOG_RED': 'Nebbia: rosso',
        'CLASSIC_FOG_GREEN': 'Nebbia: verde',
        'CLASSIC_FOG_BLUE': 'Nebbia: blu',
        'CLASSIC_FOG_DESCRIPTION': 'Vecchie mappe VXL: colore del server, del cielo o RGB '
                                   'personale. Distanza invariata. Annulla ripristina i colori '
                                   'precedenti.'},
 'pt-BR': {'INVENTORY': 'Inventário',
           'INVENTORY_DISCOVERY_HINT': 'Skins, níveis e estatísticas',
           'CLASSIC_SKY': 'Céu Classic',
           'CLASSIC_SKY_DEFAULT': 'Padrão do mapa',
           'CLASSIC_SKY_RANDOM': 'Aleatório (cores)',
           'CLASSIC_SKY_CLEAR': 'Limpo',
           'CLASSIC_SKY_OVERCAST': 'Nublado',
           'CLASSIC_SKY_DESERT': 'Deserto',
           'CLASSIC_SKY_SUNSET': 'Pôr do sol',
           'CLASSIC_SKY_NIGHT': 'Noite',
           'CLASSIC_SKY_SNOW': 'Neve',
           'CLASSIC_SKY_DESCRIPTION': 'Céu de mapas VXL antigos. Aleatório escolhe pelas cores do '
                                      'terreno uma vez por mapa. Apenas visual local.',
           'CLASSIC_FOG': 'Cor da neblina Classic',
           'CLASSIC_FOG_SERVER': 'Servidor / mapa',
           'CLASSIC_FOG_SKY': 'Conforme o céu',
           'CLASSIC_FOG_CUSTOM': 'RGB personalizado',
           'CLASSIC_FOG_RED': 'Neblina: vermelho',
           'CLASSIC_FOG_GREEN': 'Neblina: verde',
           'CLASSIC_FOG_BLUE': 'Neblina: azul',
           'CLASSIC_FOG_DESCRIPTION': 'Mapas VXL antigos: cor do servidor, do céu ou RGB próprio. '
                                      'A distância não muda. Cancelar restaura as cores '
                                      'anteriores.',
           'CLASSIC_FOG_GRAY': 'Cinza clássico (padrão)'},
 'ru': {'INVENTORY': 'Инвентарь',
        'INVENTORY_DISCOVERY_HINT': 'Скины, уровни и статистика',
        'CLASSIC_SKY': 'Небо Classic',
        'CLASSIC_SKY_DEFAULT': 'По карте',
        'CLASSIC_SKY_RANDOM': 'Случайно (цвета карты)',
        'CLASSIC_SKY_CLEAR': 'Ясно',
        'CLASSIC_SKY_OVERCAST': 'Облачно',
        'CLASSIC_SKY_DESERT': 'Пустыня',
        'CLASSIC_SKY_SUNSET': 'Закат',
        'CLASSIC_SKY_NIGHT': 'Ночь',
        'CLASSIC_SKY_SNOW': 'Снег',
        'CLASSIC_SKY_DESCRIPTION': 'Небо старых карт VXL. Случайный выбор по цветам рельефа один '
                                   'раз на карту. Только локальное оформление.',
        'CLASSIC_FOG': 'Цвет тумана Classic',
        'CLASSIC_FOG_GRAY': 'Классический серый (по умолчанию)',
        'CLASSIC_FOG_SERVER': 'Сервер / карта',
        'CLASSIC_FOG_SKY': 'По небу',
        'CLASSIC_FOG_CUSTOM': 'Свой RGB',
        'CLASSIC_FOG_RED': 'Туман: красный',
        'CLASSIC_FOG_GREEN': 'Туман: зелёный',
        'CLASSIC_FOG_BLUE': 'Туман: синий',
        'CLASSIC_FOG_DESCRIPTION': 'Старые карты VXL: цвет сервера, неба или свой RGB. Дальность '
                                   'не меняется. Отмена восстанавливает предыдущие цвета.'},
 'uk': {'INVENTORY': 'Інвентар',
        'INVENTORY_DISCOVERY_HINT': 'Скіни, рівні та статистика',
        'CLASSIC_SKY': 'Небо Classic',
        'CLASSIC_SKY_DEFAULT': 'За картою',
        'CLASSIC_SKY_RANDOM': 'Випадково (кольори)',
        'CLASSIC_SKY_CLEAR': 'Ясно',
        'CLASSIC_SKY_OVERCAST': 'Хмарно',
        'CLASSIC_SKY_DESERT': 'Пустеля',
        'CLASSIC_SKY_SUNSET': 'Захід сонця',
        'CLASSIC_SKY_NIGHT': 'Ніч',
        'CLASSIC_SKY_SNOW': 'Сніг',
        'CLASSIC_SKY_DESCRIPTION': 'Небо старих карт VXL. Випадковий вибір за кольорами рельєфу '
                                   'раз на карту. Лише локальний вигляд.',
        'CLASSIC_FOG': 'Колір туману Classic',
        'CLASSIC_FOG_SERVER': 'Сервер / карта',
        'CLASSIC_FOG_SKY': 'За небом',
        'CLASSIC_FOG_CUSTOM': 'Власний RGB',
        'CLASSIC_FOG_RED': 'Туман: червоний',
        'CLASSIC_FOG_GREEN': 'Туман: зелений',
        'CLASSIC_FOG_BLUE': 'Туман: синій',
        'CLASSIC_FOG_DESCRIPTION': 'Старі карти VXL: колір сервера, неба або власний RGB. '
                                   'Дальність не змінюється. Скасування відновлює попередні '
                                   'кольори.',
        'CLASSIC_FOG_GRAY': 'Класичний сірий (типово)'},
 'pl': {'INVENTORY': 'Ekwipunek',
        'INVENTORY_DISCOVERY_HINT': 'Skórki, poziomy i statystyki',
        'CLASSIC_SKY': 'Niebo Classic',
        'CLASSIC_SKY_DEFAULT': 'Domyślne mapy',
        'CLASSIC_SKY_RANDOM': 'Losowe (kolory mapy)',
        'CLASSIC_SKY_CLEAR': 'Pogodne',
        'CLASSIC_SKY_OVERCAST': 'Pochmurne',
        'CLASSIC_SKY_DESERT': 'Pustynia',
        'CLASSIC_SKY_SUNSET': 'Zachód słońca',
        'CLASSIC_SKY_NIGHT': 'Noc',
        'CLASSIC_SKY_SNOW': 'Śnieg',
        'CLASSIC_SKY_DESCRIPTION': 'Niebo starych map VXL. Losowanie według kolorów terenu raz na '
                                   'mapę. Tylko lokalny wygląd.',
        'CLASSIC_FOG': 'Kolor mgły Classic',
        'CLASSIC_FOG_GRAY': 'Klasyczna szarość (domyślna)',
        'CLASSIC_FOG_SERVER': 'Serwer / mapa',
        'CLASSIC_FOG_SKY': 'Według nieba',
        'CLASSIC_FOG_CUSTOM': 'Własny RGB',
        'CLASSIC_FOG_RED': 'Mgła: czerwony',
        'CLASSIC_FOG_GREEN': 'Mgła: zielony',
        'CLASSIC_FOG_BLUE': 'Mgła: niebieski',
        'CLASSIC_FOG_DESCRIPTION': 'Stare mapy VXL: kolor serwera, nieba lub własny RGB. Odległość '
                                   'bez zmian. Anuluj przywraca poprzednie kolory.'},
 'cs': {'INVENTORY': 'Inventář',
        'INVENTORY_DISCOVERY_HINT': 'Skiny, úrovně a statistiky',
        'CLASSIC_SKY': 'Obloha Classic',
        'CLASSIC_SKY_DEFAULT': 'Podle mapy',
        'CLASSIC_SKY_RANDOM': 'Náhodně (barvy mapy)',
        'CLASSIC_SKY_CLEAR': 'Jasno',
        'CLASSIC_SKY_OVERCAST': 'Zataženo',
        'CLASSIC_SKY_DESERT': 'Poušť',
        'CLASSIC_SKY_SUNSET': 'Západ slunce',
        'CLASSIC_SKY_NIGHT': 'Noc',
        'CLASSIC_SKY_SNOW': 'Sníh',
        'CLASSIC_SKY_DESCRIPTION': 'Obloha starých map VXL. Náhodný výběr podle barev terénu '
                                   'jednou za mapu. Pouze místní vzhled.',
        'CLASSIC_FOG': 'Barva mlhy Classic',
        'CLASSIC_FOG_GRAY': 'Klasická šedá (výchozí)',
        'CLASSIC_FOG_SERVER': 'Server / mapa',
        'CLASSIC_FOG_SKY': 'Podle oblohy',
        'CLASSIC_FOG_CUSTOM': 'Vlastní RGB',
        'CLASSIC_FOG_RED': 'Mlha: červená',
        'CLASSIC_FOG_GREEN': 'Mlha: zelená',
        'CLASSIC_FOG_BLUE': 'Mlha: modrá',
        'CLASSIC_FOG_DESCRIPTION': 'Staré mapy VXL: barva serveru, oblohy nebo vlastní RGB. '
                                   'Vzdálenost se nemění. Zrušit obnoví předchozí barvy.'},
 'ja': {'INVENTORY': 'インベントリ',
        'INVENTORY_DISCOVERY_HINT': 'スキン・レベル・戦績',
        'CLASSIC_SKY': 'Classicの空',
        'CLASSIC_SKY_DEFAULT': 'マップの既定',
        'CLASSIC_SKY_RANDOM': 'ランダム（地形の色）',
        'CLASSIC_SKY_CLEAR': '晴れ',
        'CLASSIC_SKY_OVERCAST': '曇り',
        'CLASSIC_SKY_DESERT': '砂漠',
        'CLASSIC_SKY_SUNSET': '夕焼け',
        'CLASSIC_SKY_NIGHT': '夜',
        'CLASSIC_SKY_SNOW': '雪',
        'CLASSIC_SKY_DESCRIPTION': '旧VXLマップの空を変更します。ランダムは地形の色を基にマップごとに一度選択します。自分の画面のみ変更します。',
        'CLASSIC_FOG': 'Classicの霧の色',
        'CLASSIC_FOG_GRAY': 'クラシックグレー（標準）',
        'CLASSIC_FOG_SERVER': 'サーバー／マップ',
        'CLASSIC_FOG_SKY': '空に合わせる',
        'CLASSIC_FOG_CUSTOM': 'RGB指定',
        'CLASSIC_FOG_RED': '霧：赤',
        'CLASSIC_FOG_GREEN': '霧：緑',
        'CLASSIC_FOG_BLUE': '霧：青',
        'CLASSIC_FOG_DESCRIPTION': '旧VXLマップ用。サーバーの色、空に合う色、またはRGBを選択します。霧の距離は変わりません。キャンセルで元の色に戻ります。'},
 'tr': {'INVENTORY': 'Envanter',
        'INVENTORY_DISCOVERY_HINT': 'Görünümler, seviye ve istatistik',
        'CLASSIC_SKY': 'Classic gökyüzü',
        'CLASSIC_SKY_DEFAULT': 'Harita varsayılanı',
        'CLASSIC_SKY_RANDOM': 'Rastgele (harita rengi)',
        'CLASSIC_SKY_CLEAR': 'Açık',
        'CLASSIC_SKY_OVERCAST': 'Bulutlu',
        'CLASSIC_SKY_DESERT': 'Çöl',
        'CLASSIC_SKY_SUNSET': 'Gün batımı',
        'CLASSIC_SKY_NIGHT': 'Gece',
        'CLASSIC_SKY_SNOW': 'Kar',
        'CLASSIC_SKY_DESCRIPTION': 'Eski VXL haritalarının gökyüzü. Rastgele seçim arazi '
                                   'renklerine göre harita başına bir kez yapılır. Yalnızca yerel '
                                   'görünüm.',
        'CLASSIC_FOG': 'Classic sis rengi',
        'CLASSIC_FOG_GRAY': 'Klasik gri (varsayılan)',
        'CLASSIC_FOG_SERVER': 'Sunucu / harita',
        'CLASSIC_FOG_SKY': 'Gökyüzüne göre',
        'CLASSIC_FOG_CUSTOM': 'Özel RGB',
        'CLASSIC_FOG_RED': 'Sis: kırmızı',
        'CLASSIC_FOG_GREEN': 'Sis: yeşil',
        'CLASSIC_FOG_BLUE': 'Sis: mavi',
        'CLASSIC_FOG_DESCRIPTION': 'Eski VXL haritaları: sunucu rengi, gökyüzü veya özel RGB. Sis '
                                   'mesafesi değişmez. İptal önceki renkleri geri getirir.'},
 'es-MX': {'INVENTORY': 'Inventario',
           'INVENTORY_DISCOVERY_HINT': 'Skins, niveles y estadísticas',
           'CLASSIC_SKY': 'Cielo Classic',
           'CLASSIC_SKY_DEFAULT': 'Según el mapa',
           'CLASSIC_SKY_RANDOM': 'Aleatorio (colores)',
           'CLASSIC_SKY_CLEAR': 'Despejado',
           'CLASSIC_SKY_OVERCAST': 'Nublado',
           'CLASSIC_SKY_DESERT': 'Desierto',
           'CLASSIC_SKY_SUNSET': 'Atardecer',
           'CLASSIC_SKY_NIGHT': 'Noche',
           'CLASSIC_SKY_SNOW': 'Nieve',
           'CLASSIC_SKY_DESCRIPTION': 'Cielo de mapas VXL antiguos. Aleatorio elige según los '
                                      'colores del terreno una vez por mapa. Solo apariencia '
                                      'local.',
           'CLASSIC_FOG': 'Color de niebla Classic',
           'CLASSIC_FOG_SERVER': 'Servidor / mapa',
           'CLASSIC_FOG_SKY': 'Según el cielo',
           'CLASSIC_FOG_CUSTOM': 'RGB personalizado',
           'CLASSIC_FOG_RED': 'Niebla: rojo',
           'CLASSIC_FOG_GREEN': 'Niebla: verde',
           'CLASSIC_FOG_BLUE': 'Niebla: azul',
           'CLASSIC_FOG_DESCRIPTION': 'Mapas VXL antiguos: color del servidor, del cielo o RGB '
                                      'propio. La distancia no cambia. Cancelar restaura los '
                                      'colores anteriores.',
           'CLASSIC_FOG_GRAY': 'Gris clásico (predeterminado)'}}


def source_directory(argument: Path) -> Path:
    """Accept the historical english.py argument or the strings directory."""

    return argument.parent if argument.is_file() else argument


def main() -> None:
    """Generate all configured language files and validate retail coverage."""

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        required=True,
        help="Recovered aoslib/strings directory (or its english.py for compatibility)",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    strings_root = source_directory(args.source)
    english = recovered_strings(strings_root / "english.py")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    translated_count = 0
    for locale, metadata in LOCALES.items():
        if metadata.source_module is not None:
            strings = recovered_strings(strings_root / metadata.source_module)
            missing = english.keys() - strings.keys()
            if missing:
                preview = ", ".join(sorted(missing)[:5])
                raise ValueError(
                    f"{metadata.source_module} is missing {len(missing)} retail keys: "
                    f"{preview}"
                )
            translated_count += locale != "en"
        else:
            strings = COMMUNITY_OVERLAYS.get(locale, {})

        if locale == "ja":
            strings = repair_recovered_japanese(strings)
            validate_japanese_source(strings)
        elif locale == "tr":
            strings = {
                key: _replace_all(value, TURKISH_TEXT_REPLACEMENTS)
                for key, value in strings.items()
            }

        # Copy before applying revival-only keys so the parsed source mapping
        # stays immutable and can still be compared against retail coverage.
        strings = dict(strings)
        strings.update(CLIENT_OVERLAYS.get(locale, {}))
        strings.update(MAJOR_CLIENT_OVERLAYS.get(locale, {}))
        strings.update(NATIVE_FLOW_OVERLAYS.get(locale, {}))
        strings.update(APPEARANCE_SETTINGS_OVERLAYS.get(locale, {}))
        strings.update(PARITY_HUD_OVERLAYS.get(locale, {}))
        strings.update(PRESENTATION_SETTINGS_OVERLAYS.get(locale, {}))
        strings.update(SERVER_JOIN_OVERLAYS.get(locale, {}))
        strings.update(BLOOD_SETTINGS_OVERLAYS.get(locale, {}))
        strings.update(DISCORD_SETTINGS_OVERLAYS.get(locale, {}))
        strings.update(CLASSIC_APPEARANCE_OVERLAYS.get(locale, {}))

        document = {
            "schema_version": 1,
            "locale": locale,
            "native_name": metadata.native_name,
            "font_asset": metadata.font_asset,
            "strings": strings,
        }
        output = args.output_dir / f"{locale}.json"
        output.write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    print(
        f"wrote {len(LOCALES)} UTF-8 language files to {args.output_dir}; "
        f"{translated_count} recovered translations, {len(english)} English keys"
    )


if __name__ == "__main__":
    main()
