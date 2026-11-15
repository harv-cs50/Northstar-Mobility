/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,jsx}'],
  theme: {
    extend: {
      colors: {
        ink: '#17251f',
        forest: '#183d31',
        lime: '#c4ed72',
        paper: '#f7f8f6',
      },
      fontFamily: { sans: ['Inter', 'ui-sans-serif', 'system-ui'] },
    },
  },
  plugins: [],
};
