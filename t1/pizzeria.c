#include "pizzeria.h"

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "helper.h"


static void* cozinhar(void* arg);
static void* atender(void* arg);

static const int TAM_MESA = 4;

static bool pizzaria_fechada;

static sem_t balcao_espaco;
static sem_t balcao_ocupado;
static pizza_t* balcao_pizza;

static pthread_mutex_t pa;

static sem_t forno_espacos;

static pthread_mutex_t mesas;
static int mesas_livres;
static int mesas_total;

static queue_t deck;

static sem_t grupos_concorrentes;

static pthread_t* pizzaiolos;
static int pizzaiolos_size;

static pthread_t* garcons;
static int garcons_size;
static sem_t garcons_desocupados;


// INFRAESTRUTURA

void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos) {
	assert(tam_forno > 0);
	assert(n_pizzaiolos > 0);
	assert(n_mesas > 0);
	assert(n_garcons > 0);
	assert(tam_deck > 0);
	assert(n_grupos > 0);

	pizzaria_fechada = false;

	sem_init(&balcao_espaco, false, 1);
	sem_init(&balcao_ocupado, false, 0);

	pthread_mutex_init(&pa, NULL);

	sem_init(&forno_espacos, false, tam_forno);

	mesas_total = n_mesas;
	mesas_livres = n_mesas;
	pthread_mutex_init(&mesas, NULL);

	queue_init(&deck, tam_deck);

	sem_init(&grupos_concorrentes, false, n_grupos);

	pizzaiolos = malloc(sizeof(pthread_t) * n_pizzaiolos);
	assert(pizzaiolos != NULL);
	pizzaiolos_size = n_pizzaiolos;
	for (int i = 0; i < pizzaiolos_size; ++i)
		pthread_create(&pizzaiolos[i], NULL, cozinhar, NULL);

	sem_init(&garcons_desocupados, false, n_garcons);
	garcons = malloc(sizeof(pthread_t) * n_garcons);
	assert(garcons != NULL);
	garcons_size = n_garcons;
	for (int i = 0; i < garcons_size; ++i)
		pthread_create(&garcons[i], NULL, atender, NULL);
}

static bool pizzaria_vazia(void) {
	pthread_mutex_lock(&mesas);
	const int livres = mesas_livres;
	pthread_mutex_unlock(&mesas);
	return livres >= mesas_total;
}

void pizzeria_close(void) {
	// fecha a entrada
	pizzaria_fechada = true;

	// espera os clientes sairem
	while(!pizzaria_vazia());

	// @TODO: saida dos funcionarios

	// // espera os garcons sairem
	// for (int i = 0; i < garcons_size; ++i)
	// 	pthread_join(garcons[i], NULL);

	// // espera os pizzaiolos sairem
	// for (int i = 0; i < pizzaiolos_size; ++i)
	// 	pthread_join(pizzaiolos[i], NULL);
}

void pizzeria_destroy(void) {
	free(garcons);

	free(pizzaiolos);

	sem_destroy(&garcons_desocupados);

	sem_destroy(&grupos_concorrentes);

	queue_destroy(&deck);

	pthread_mutex_destroy(&mesas);

	sem_destroy(&forno_espacos);

	pthread_mutex_destroy(&pa);

	sem_destroy(&balcao_espaco);
	sem_destroy(&balcao_ocupado);
}


// FUNCIONARIOS
// Comportamento de cada um dos pizzaiolos.
static void* cozinhar(void* arg) {
	while (true) {
		// pega um pedido do smartdeck
		pedido_t* pedido = queue_wait(&deck);

		// monta o pedido
		pizza_t* pizza = pizzaiolo_montar_pizza(pedido);
		sem_init(&(pizza->pronta), false, 0);

		// espera um espaco no forno
		sem_wait(&forno_espacos);
		// usa a pa para colocar a pizza no forno
		pthread_mutex_lock(&pa);
		pizzaiolo_colocar_forno(pizza);
		pthread_mutex_unlock(&pa);

		// espera ate que a pizza esteja pronta
		sem_wait(&(pizza->pronta));

		// espera espaco no balcao
		sem_wait(&balcao_espaco);
		// usa a pa para tirar a pizza do forno (liberando espaco) e botar no balcao
		pthread_mutex_lock(&pa);
		pizzaiolo_retirar_forno(pizza);
		sem_post(&forno_espacos);
		pthread_mutex_unlock(&pa);

		// finalizando o preparo
		sem_destroy(&(pizza->pronta));

		// coloca um pegador junto a pizza no balcao
		balcao_pizza = pizza;
		// @FIXME: esse mutex nunca eh destruido
		pthread_mutex_init(&(balcao_pizza->pegador), NULL);
		sem_post(&balcao_ocupado);
	}
	return NULL;
}

void pizza_assada(pizza_t* pizza) {
	// indica que a pizza esta pronta
	sem_post(&(pizza->pronta));
}

// Comportamento de cada um dos garcons.
static void* atender(void* arg) {
	while (true) {
		// espera ate que um garcom esteja livre
		sem_wait(&garcons_desocupados);

		// confere se tem que levar algum pedido do balcao para mesas
		if (!sem_trywait(&balcao_ocupado)) {
			// entrega a pizza e libera espaco do balcao
			garcom_entregar(balcao_pizza);
			sem_post(&balcao_espaco);
		}

		// libera garcom
		sem_post(&garcons_desocupados);
	}
	return NULL;
}


// CLIENTES

int pegar_mesas(int tam_grupo) {
	sem_wait(&grupos_concorrentes); // limita o numero de grupos tentando sentar
	int mesas_necessarias = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	// enquanto nao estiverem sentados
	while (mesas_necessarias > 0) {
		// se a pizzaria estiver aberta
		if (!pizzaria_fechada) {
			// tenta sentar ocupando as mesas necessarias
			pthread_mutex_lock(&mesas);
			if (mesas_necessarias <= mesas_livres) {
				mesas_livres -= mesas_necessarias;
				mesas_necessarias = 0;
			}
			pthread_mutex_unlock(&mesas);
		} else {
			// pizaria fechada -> desistem
			sem_post(&grupos_concorrentes);
			return -1;
		}
	}
	// libera a entrada um grupo futuro
	sem_post(&grupos_concorrentes);
	return 0;
}

void fazer_pedido(pedido_t* pedido) {
	// manda o pedido para o smartdeck
	queue_push_back(&deck, pedido);
}

int pizza_pegar_fatia(pizza_t* pizza) {
	// espera o pegador
	pthread_mutex_lock(&(pizza->pegador));
	// se ainda houver fatia
	if (pizza->fatias >= 1) {
		// come e solta o pegador
		pizza->fatias--;
		pthread_mutex_unlock(&(pizza->pegador));
		return 0;
	} else {
		// solta o pegador e para de comer
		pthread_mutex_unlock(&(pizza->pegador));
		return -1;
	}
}

void garcom_chamar(void) {
	// chama um garcom para a mesa
	sem_wait(&garcons_desocupados);
}

void garcom_tchau(int tam_grupo) {
	// libera o garcom que chamou
	sem_post(&garcons_desocupados);

	// desocupam as mesas em que estavam
	const int mesas_ocupadas = tam_grupo / TAM_MESA + (tam_grupo % TAM_MESA != 0);
	pthread_mutex_lock(&mesas);
	mesas_livres += mesas_ocupadas;
	pthread_mutex_unlock(&mesas);
}
